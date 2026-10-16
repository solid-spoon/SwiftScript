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
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

#define PATHBUF 4096
#define MAXPATH (PATHBUF * 4)

#if defined(_WIN32) || defined(_WIN64)
#include <direct.h>
#define mkdir(path, mode) _mkdir(path)
#endif


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
static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = xmalloc(n + 1);
    size_t got = fread(buf, 1, n, f);
    buf[got] = '\0';
    fclose(f);
    return buf;
}

static int write_file(const char *path, const char *data) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = strlen(data);
    size_t w = fwrite(data, 1, n, f);
    fclose(f);
    return (w == n) ? 0 : -1;
}

static int mkdir_p(const char *path) {
    char tmp[MAXPATH];
    size_t len = strlen(path);
    if (len >= sizeof(tmp)) return -1;
    memcpy(tmp, path, len + 1);
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';
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

/* foo.sws → foo.js,  foo → foo.js */
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
            strncmp(line, "export ",   7) == 0 ||
            strncmp(line, "export\t",   7) == 0 ||
            strncmp(line, "import{",    7) == 0 ||
            strncmp(line, "export{",    7) == 0)
            return 1;
        while (*p && *p != '\n') p++;
        if (*p == '\n') p++;
    }
    return 0;
}

static int compile_file(const char *src_path, const char *out_path) {
    char *src = read_file(src_path);
    if (!src) { fprintf(stderr, "swsc: cannot read %s\n", src_path); return 1; }

    SSError err = {0};
    TokenList *toks = tokenize(src, &err);
    if (!toks) {
        fprintf(stderr, "%s:%d:%d: %s\n", src_path, err.line, err.col, err.msg);
        free(src); return 1;
    }
    Node *ast = parse(toks, &err);
    free_tokens(toks);
    if (!ast) {
        fprintf(stderr, "%s:%d:%d: %s\n", src_path, err.line, err.col, err.msg);
        free(src); return 1;
    }
    char *user_js = gen_program(ast);

    StrBuf out; sb_init(&out);
    sb_put(&out, SWIFTS_RUNTIME);
    sb_put(&out, "\n");
    if (has_module_syntax(user_js)) {
        sb_put(&out, user_js);
    } else {
        sb_put(&out, "(() => {\n");
        sb_put(&out, user_js);
        sb_put(&out, "\n})();\n");
    }
    sb_put(&out, "\n");

    /* ensure dir */
    char dir[MAXPATH];
    snprintf(dir, sizeof(dir), "%s", out_path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = '\0'; mkdir_p(dir); }

    int rc = write_file(out_path, out.buf);
    if (rc != 0) fprintf(stderr, "swsc: cannot write %s: %s\n", out_path, strerror(errno));

    sb_free(&out);
    free(user_js);
    free(src);
    return rc ? 1 : 0;
}

static int walk(const char *src_root, const char *out_root, const char *rel) {
    char src_dir[MAXPATH], out_dir[MAXPATH];
    if (*rel) {
        snprintf(src_dir, sizeof(src_dir), "%s/%s", src_root, rel);
        snprintf(out_dir, sizeof(out_dir), "%s/%s", out_root, rel);
    } else {
        snprintf(src_dir, sizeof(src_dir), "%s", src_root);
        snprintf(out_dir, sizeof(out_dir), "%s", out_root);
    }

    DIR *d = opendir(src_dir);
    if (!d) { fprintf(stderr, "swsc: cannot open %s\n", src_dir); return 1; }
    int errors = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
        char child_rel[MAXPATH];
        if (*rel) snprintf(child_rel, sizeof(child_rel), "%s/%s", rel, e->d_name);
        else       snprintf(child_rel, sizeof(child_rel), "%s", e->d_name);

        char full[MAXPATH];
        int r = snprintf(full, sizeof(full), "%s/%s", src_root, child_rel);
        if (r < 0 || (size_t)r >= sizeof(full)) {
            fprintf(stderr, "swsc: path too long, skipping %s/%s\n",
                    src_root, child_rel);
            continue;
        }
        struct stat st;
        if (stat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            errors += walk(src_root, out_root, child_rel);
        } else if (S_ISREG(st.st_mode) && strcmp(path_ext(e->d_name), ".sws") == 0) {
            /* out path: same rel but .js */
            char out_rel[MAXPATH];
            snprintf(out_rel, sizeof(out_rel), "%s", child_rel);
            size_t n = strlen(out_rel);
            out_rel[n - 4] = '\0'; /* strip .sws */
            strncat(out_rel, ".js", sizeof(out_rel) - strlen(out_rel) - 1);

            char out_path[MAXPATH];
            snprintf(out_path, sizeof(out_path), "%s/%s", out_root, out_rel);
            mkdir_p(out_root);
            errors += compile_file(full, out_path);
        }
    }
    closedir(d);
    return errors;
}

int main(int argc, char **argv) {
    if (argc < 3 || strcmp(argv[1], "build") != 0) {
        fprintf(stderr,
            "usage: %s build <path> [-o <out>]\n"
            "  Compile every .sws file under <path> into .js.\n"
            "  Without -o, output goes next to the input (.sws -> .js).\n",
            argv[0]);
        return 2;
    }

    const char *src = argv[2];
    const char *out = NULL;

    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "swsc: -o requires an argument\n");
                return 2;
            }
            out = argv[++i];
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
                char base_js[4096];
                derive_js_path(base, base_js, sizeof(base_js));
                snprintf(derived, sizeof(derived), "%s/%s", out, base_js);
                out = derived;
            }
        }
        return compile_file(src, out) ? 1 : 0;
    }

    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "swsc: %s is neither a file nor a directory\n", src);
        return 2;
    }

    if (!out) out = src;
    mkdir_p(out);
    int rc = walk(src, out, "");
    if (rc) fprintf(stderr, "swsc: %d error(s)\n", rc);
    return rc ? 1 : 0;
}