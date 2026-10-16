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

#define PATHBUF 8192

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
    char tmp[8192];
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
    sb_put(&out, user_js);
    sb_put(&out, "\n");

    /* ensure dir */
    char dir[8192];
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
    char src_dir[8192], out_dir[8192];
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
        char child_rel[8192];
        if (*rel) snprintf(child_rel, sizeof(child_rel), "%s/%s", rel, e->d_name);
        else       snprintf(child_rel, sizeof(child_rel), "%s", e->d_name);

        char full[8192];
        snprintf(full, sizeof(full), "%s/%s", src_root, child_rel);
        struct stat st;
        if (stat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            errors += walk(src_root, out_root, child_rel);
        } else if (S_ISREG(st.st_mode) && strcmp(path_ext(e->d_name), ".sws") == 0) {
            /* out path: same rel but .js */
            char out_rel[8192];
            snprintf(out_rel, sizeof(out_rel), "%s", child_rel);
            size_t n = strlen(out_rel);
            out_rel[n - 4] = '\0'; /* strip .sws */
            strncat(out_rel, ".js", sizeof(out_rel) - strlen(out_rel) - 1);

            char out_path[8192];
            snprintf(out_path, sizeof(out_path), "%s/%s", out_root, out_rel);
            mkdir_p(out_root);
            errors += compile_file(full, out_path);
        }
    }
    closedir(d);
    return errors;
}

int main(int argc, char **argv) {
    if (argc != 5 || strcmp(argv[1], "build") != 0 || strcmp(argv[3], "-o") != 0) {
        fprintf(stderr,
            "usage: %s build <src_dir> -o <out_dir>\n"
            "  Compiles every .sws file under <src_dir> into .js preserving structure.\n",
            argv[0]);
        return 2;
    }
    const char *src = argv[2];
    const char *out = argv[4];
    mkdir_p(out);
    int rc = walk(src, out, "");
    if (rc) fprintf(stderr, "swsc: %d error(s)\n", rc);
    return rc ? 1 : 0;
}