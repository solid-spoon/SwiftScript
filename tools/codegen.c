#include "codegen.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static int _sw_counter = 0;
static int _let_bind_counter = 0;

/* codegen diagnostics: set once, first error wins. */
typedef struct {
    int has_err;
    int line, col;
    char msg[256];
} GenError;

static GenError _gen;

static void codegen_error(int line, int col, const char *fmt, ...) {
    if (_gen.has_err) return;
    _gen.line = line;
    _gen.col = col;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(_gen.msg, sizeof(_gen.msg), fmt, ap);
    va_end(ap);
    _gen.has_err = 1;
}

int         gen_had_error(void) { return _gen.has_err; }
const char *gen_error_msg(void) { return _gen.msg; }
int         gen_error_line(void){ return _gen.line; }
int         gen_error_col(void) { return _gen.col; }

typedef struct { char **items; int count, cap; } SS;

static void ss_add(SS *s, const char *v) {
    for (int i = 0; i < s->count; i++) if (strcmp(s->items[i], v) == 0) return;
    if (s->count == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 8;
        s->items = xrealloc(s->items, sizeof(char*) * s->cap);
    }
    s->items[s->count++] = xstrdup(v);
}

static int ss_has(SS *s, const char *v) {
    if (!s) return 0;
    for (int i = 0; i < s->count; i++) if (strcmp(s->items[i], v) == 0) return 1;
    return 0;
}

static SS ss_clone(SS *src) {
    SS c = {0};
    for (int i = 0; i < src->count; i++) ss_add(&c, src->items[i]);
    return c;
}

typedef struct {
    SS struct_names;
    struct Pair { char *k; char *v; } *enum_cases;
    int enum_case_count, enum_case_cap;
    SS fields;
    const char *self_prefix;   /* NULL | "_self" | "this" */
    SS locals;
    SS break_stack;
    const char *switch_label;
} Ctx;

static void enum_add(Ctx *c, const char *case_name, const char *enum_name) {
    if (c->enum_case_count == c->enum_case_cap) {
        c->enum_case_cap = c->enum_case_cap ? c->enum_case_cap * 2 : 8;
        c->enum_cases = xrealloc(c->enum_cases, sizeof(*c->enum_cases) * c->enum_case_cap);
    }
    c->enum_cases[c->enum_case_count].k = xstrdup(case_name);
    c->enum_cases[c->enum_case_count].v = xstrdup(enum_name);
    c->enum_case_count++;
}

static const char *enum_find(Ctx *c, const char *case_name) {
    for (int i = c->enum_case_count - 1; i >= 0; i--)
        if (strcmp(c->enum_cases[i].k, case_name) == 0) return c->enum_cases[i].v;
    return NULL;
}

static Ctx ctx_copy(Ctx *src) {
    Ctx c = *src;
    c.locals      = ss_clone(&src->locals);
    c.break_stack = ss_clone(&src->break_stack);
    c.fields      = ss_clone(&src->fields);
    /* struct_names and enum_cases are read-only */
    return c;
}

/* forward */
static char *gen_stmt(Node *n, Ctx *ctx);
static char *gen_expr(Node *n, Ctx *ctx);
static char *gen_import(Node *n);
static char *gen_class(Node *n, Ctx *ctx);
static char *gen_if_stmt(Node *n, Ctx *ctx);
static char *gen_switch(Node *n, Ctx *ctx);
static char *gen_struct(Node *n, Ctx *ctx);
static char *gen_enum(Node *n, Ctx *ctx);
static char *block_returning(List *stmts, Ctx *ctx);
static char *gen_if_let_stmt(Node *n, Ctx *ctx);
static char *gen_guard_stmt(Node *n, Ctx *ctx);
static int   block_terminates(List *stmts);
static char *gen_call(Node *n, Ctx *ctx);
static char *gen_closure(Node *n, Ctx *ctx);
static char *gen_template(Node *n, Ctx *ctx);
static char *gen_if_expr(Node *n, Ctx *ctx);
static char *gen_pattern(Node *p, const char *subj, Ctx *ctx, char **bindings_out, int *bindings_count);

static void collect_decls(Node *prog, Ctx *ctx) {
    for (int i = 0; i < prog->stmts->count; i++) {
        Node *s = prog->stmts->items[i];
        if (s->kind == N_STRUCT) ss_add(&ctx->struct_names, s->name);
        if (s->kind == N_ENUM) {
            for (int j = 0; j < s->cases->count; j++) {
                Node *c = s->cases->items[j];
                enum_add(ctx, c->name, s->name);
            }
        }
    }
}

/* ---------- stmt ---------- */

static char *gen_stmt(Node *n, Ctx *ctx) {
    StrBuf sb; sb_init(&sb);
    switch (n->kind) {
    case N_IMPORT: return gen_import(n);
    case N_LET: {
        const char *kw = (n->sval && strcmp(n->sval, "var") == 0) ? "let" : "const";
        char *v = n->a ? gen_expr(n->a, ctx) : xstrdup("undefined");
        ss_add(&ctx->locals, n->name);
        if (n->exported) sb_put(&sb, "export ");
        sb_printf(&sb, "%s %s = %s;", kw, n->name, v);
        free(v);
        break;
    }
    case N_FUNC: {
        StrBuf ps; sb_init(&ps);
        for (int i = 0; i < n->params->count; i++) {
            if (i) sb_put(&ps, ", ");
            sb_put(&ps, ((Node*)n->params->items[i])->name);
        }
        Ctx inner = ctx_copy(ctx);
        inner.fields.count = 0;
        inner.self_prefix = NULL;
        SS newlocals = {0};
        for (int i = 0; i < n->params->count; i++)
            ss_add(&newlocals, ((Node*)n->params->items[i])->name);
        inner.locals = newlocals;
        StrBuf body; sb_init(&body);
        for (int i = 0; i < n->body->count; i++) {
            if (i) sb_putc(&body, '\n');
            char *s = gen_stmt(n->body->items[i], &inner);
            sb_put(&body, s); free(s);
        }
        char *pstr = sb_take(&ps);
        char *bstr = sb_take(&body);
        if (n->exported) sb_put(&sb, "export ");
        if (n->is_async)  sb_put(&sb, "async ");
        sb_printf(&sb, "function %s(%s) {\n%s\n}", n->name, pstr, bstr);
        free(pstr); free(bstr);
        break;
    }
    case N_STRUCT: return gen_struct(n, ctx);
    case N_ENUM:   return gen_enum(n, ctx);
    case N_CLASS:  return gen_class(n, ctx);
    case N_RETURN:
        if (n->a) { char *e = gen_expr(n->a, ctx); sb_printf(&sb, "return %s;", e); free(e); }
        else sb_put(&sb, "return;");
        break;
    case N_EXPR_STMT:
        if (n->a->kind == N_IF)      return gen_if_stmt(n->a, ctx);
        if (n->a->kind == N_IF_LET)  return gen_if_let_stmt(n->a, ctx);
        if (n->a->kind == N_SWITCH)  return gen_switch(n->a, ctx);
        { char *e = gen_expr(n->a, ctx); sb_printf(&sb, "%s;", e); free(e); }
        break;
    case N_GUARD_LET: return gen_guard_stmt(n, ctx);
    case N_BLOCK: {
        sb_put(&sb, "{ ");
        for (int i = 0; i < n->stmts->count; i++) {
            if (i) sb_putc(&sb, ' ');
            char *s = gen_stmt(n->stmts->items[i], ctx);
            sb_put(&sb, s); free(s);
        }
        sb_put(&sb, " }");
        break;
    }
    case N_IF: return gen_if_stmt(n, ctx);
    case N_WHILE: {
        Ctx inner = ctx_copy(ctx);
        ss_add(&inner.break_stack, "loop");
        char *c = gen_expr(n->a, ctx);
        char *b = gen_stmt(n->b, &inner);
        sb_printf(&sb, "while (%s) %s", c, b);
        free(c); free(b);
        break;
    }
    case N_FOR: {
        Ctx inner = ctx_copy(ctx);
        ss_add(&inner.break_stack, "loop");
        ss_add(&inner.locals, n->name);
        char *src = gen_expr(n->a, ctx);
        char *b = gen_stmt(n->b, &inner);
        sb_printf(&sb, "for (const %s of %s) %s", n->name, src, b);
        free(src); free(b);
        break;
    }
    case N_TRY: {
        char *body = gen_stmt(n->a, ctx);
        char *handler = gen_stmt(n->b, ctx);
        const char *cvar = n->name ? n->name : "_e";
        sb_printf(&sb, "try %s catch (%s) %s", body, cvar, handler);
        free(body); free(handler);
        break;
    }
    case N_THROW: {
        if (n->a) {
            char *e = gen_expr(n->a, ctx);
            sb_printf(&sb, "throw %s;", e);
            free(e);
        } else sb_put(&sb, "throw;");
        break;
    }
    case N_BREAK: {
        const char *top = ctx->break_stack.count ?
            ctx->break_stack.items[ctx->break_stack.count - 1] : NULL;
        if (top && strcmp(top, "switch") == 0)
            sb_printf(&sb, "break %s;", ctx->switch_label);
        else sb_put(&sb, "break;");
        break;
    }
    case N_CONTINUE: sb_put(&sb, "continue;"); break;
    default:
        codegen_error(n->line, n->col, "codegen: unknown statement kind %d", n->kind);
        sb_printf(&sb, "/* codegen error */");
    }
    return sb_take(&sb);
}

static char *gen_if_stmt(Node *n, Ctx *ctx) {
    StrBuf sb; sb_init(&sb);
    char *c = gen_expr(n->a, ctx);
    char *t = gen_stmt(n->b, ctx);
    sb_printf(&sb, "if (%s) %s", c, t);
    free(c); free(t);
    if (n->c) {
        if (n->c->kind == N_IF) {
            char *e = gen_if_stmt(n->c, ctx);
            sb_printf(&sb, " else %s", e);
            free(e);
        } else {
            char *e = gen_stmt(n->c, ctx);
            sb_printf(&sb, " else %s", e);
            free(e);
        }
    }
    return sb_take(&sb);
}

static char *gen_if_let_stmt(Node *n, Ctx *ctx) {
    StrBuf sb; sb_init(&sb);
    int id = ++_let_bind_counter;
    int count = n->items->count;
    int has_else = (n->c != NULL);

    sb_put(&sb, "{\n");

    if (has_else)
        sb_printf(&sb, "  let _ok%d = false;\n", id);

    for (int i = 0; i < count; i++) {
        Node *b = n->items->items[i];
        char *v = gen_expr(b->a, ctx);
        const char *kw = (b->sval && strcmp(b->sval, "var") == 0) ? "let" : "const";

        sb_printf(&sb, "  const _t%d_%d = %s;\n", id, i, v);
        sb_printf(&sb, "  if (_t%d_%d != null) {\n", id, i);
        sb_printf(&sb, "    %s %s = _t%d_%d;\n", kw, b->name, id, i);

        ss_add(&ctx->locals, b->name);
        free(v);
    }

    if (n->a) {
        char *c = gen_expr(n->a, ctx);
        sb_printf(&sb, "  if (%s) {\n", c);
        free(c);
    }

    if (has_else) sb_printf(&sb, "    _ok%d = true;\n", id);

    if (n->b && n->b->kind == N_BLOCK) {
        for (int i = 0; i < n->b->stmts->count; i++) {
            char *s = gen_stmt(n->b->stmts->items[i], ctx);
            sb_printf(&sb, "    %s\n", s);
            free(s);
        }
    }

    if (n->a) sb_put(&sb, "  }\n");
    for (int i = 0; i < count; i++) sb_put(&sb, "  }\n");

    if (has_else) {
        sb_printf(&sb, "  if (!_ok%d) ", id);
        if (n->c->kind == N_IF) {
            char *e = gen_if_stmt(n->c, ctx);
            sb_printf(&sb, "%s\n", e);
            free(e);
        } else if (n->c->kind == N_IF_LET) {
            char *e = gen_if_let_stmt(n->c, ctx);
            sb_printf(&sb, "%s\n", e);
            free(e);
        } else {
            char *e = gen_stmt(n->c, ctx);
            sb_printf(&sb, "%s\n", e);
            free(e);
        }
    }

    sb_put(&sb, "}");
    return sb_take(&sb);
}

static char *gen_guard_stmt(Node *n, Ctx *ctx) {
    if (n->b && n->b->kind == N_BLOCK && !block_terminates(n->b->stmts)) {
        codegen_error(n->line, n->col,
            "guard else-block must end with return, throw, break, or continue");
    }
    StrBuf sb; sb_init(&sb);
    int id = ++_let_bind_counter;
    int count = n->items->count;

    if (count > 0) {
        sb_put(&sb, "let ");
        for (int i = 0; i < count; i++) {
            if (i) sb_put(&sb, ", ");
            sb_printf(&sb, "_g%d_%d", id, i);
        }
        sb_put(&sb, ";\n");
    }

    if (count > 0) {
        sb_put(&sb, "if (");
        for (int i = 0; i < count; i++) {
            Node *b = n->items->items[i];
            char *v = gen_expr(b->a, ctx);
            if (i) sb_put(&sb, " || ");
            sb_printf(&sb, "((_g%d_%d = %s) == null)", id, i, v);
            free(v);
        }
        sb_put(&sb, ") ");
        char *e = gen_stmt(n->b, ctx);
        sb_printf(&sb, "%s\n", e);
        free(e);

        for (int i = 0; i < count; i++) {
            Node *b = n->items->items[i];
            const char *kw = (b->sval && strcmp(b->sval, "var") == 0) ? "let" : "const";
            sb_printf(&sb, "%s %s = _g%d_%d;\n", kw, b->name, id, i);
            ss_add(&ctx->locals, b->name);
        }
    }

    if (n->a) {
        char *c = gen_expr(n->a, ctx);
        sb_printf(&sb, "if (!(%s)) ", c);
        char *e = gen_stmt(n->b, ctx);
        sb_printf(&sb, "%s\n", e);
        free(e);
        free(c);
    }

    return sb_take(&sb);
}

static int block_terminates(List *stmts) {
    if (!stmts || stmts->count == 0) return 0;
    Node *last = stmts->items[stmts->count - 1];
    switch (last->kind) {
    case N_RETURN:
    case N_THROW:
    case N_BREAK:
    case N_CONTINUE:
        return 1;
    case N_EXPR_STMT: {
        if (!last->a) return 0;
        Node *e = last->a;
        if (e->kind == N_IF) {
            if (!e->c) return 0;
            if (!e->b || e->b->kind != N_BLOCK) return 0;
            if (!block_terminates(e->b->stmts)) return 0;
            if (e->c->kind == N_IF) return 0;
            if (e->c->kind != N_BLOCK) return 0;
            return block_terminates(e->c->stmts);
        }
        if (e->kind == N_SWITCH) {
            if (!e->body) return 0;
            if (!block_terminates(e->body)) return 0;
            for (int i = 0; i < e->cases->count; i++) {
                Node *c = e->cases->items[i];
                if (!block_terminates(c->body)) return 0;
            }
            return 1;
        }
        return 0;
    }
    default:
        return 0;
    }
}

static char *block_returning(List *stmts, Ctx *ctx) {
    StrBuf sb; sb_init(&sb);
    if (stmts->count == 0) return xstrdup("return null;");
    for (int i = 0; i < stmts->count; i++) {
        Node *s = stmts->items[i];
        int is_last = (i == stmts->count - 1);
        if (is_last && s->kind == N_EXPR_STMT) {
            char *e = gen_expr(s->a, ctx);
            sb_printf(&sb, "return %s;", e);
            free(e);
        } else {
            char *c = gen_stmt(s, ctx);
            sb_put(&sb, c); free(c);
        }
        if (i < stmts->count - 1) sb_putc(&sb, ' ');
    }
    return sb_take(&sb);
}

static char *gen_import(Node *n) {
    StrBuf sb; sb_init(&sb);
    sb_put(&sb, "import ");

    if (strcmp(n->op, "namespace") == 0) {
        Node *spec = n->items->items[0];
        sb_printf(&sb, "* as %s", spec->name);
    } else if (strcmp(n->op, "default") == 0) {
        Node *spec = n->items->items[0];
        sb_put(&sb, spec->name);
    } else {
        sb_put(&sb, "{ ");
        for (int i = 0; i < n->items->count; i++) {
            if (i) sb_put(&sb, ", ");
            Node *spec = n->items->items[i];
            sb_put(&sb, spec->name);
            if (spec->sval) sb_printf(&sb, " as %s", spec->sval);
        }
        sb_put(&sb, " }");
    }

    const char *path = n->name;
    size_t plen = strlen(path);
    StrBuf mod; sb_init(&mod);
    if (plen >= 4 && strcmp(path + plen - 4, ".sws") == 0) {
        sb_putn(&mod, path, plen - 4);
        sb_put(&mod, ".js");
    } else {
        sb_put(&mod, path);
    }

    sb_put(&sb, " from \"");
    for (const char *s = mod.buf; *s; s++) {
        if (*s == '"' || *s == '\\') sb_putc(&sb, '\\');
        sb_putc(&sb, *s);
    }
    sb_put(&sb, "\";");

    sb_free(&mod);
    return sb_take(&sb);
}

static char *gen_enum(Node *n, Ctx *ctx) {
    (void)ctx;
    StrBuf sb; sb_init(&sb);
    if (n->exported) sb_put(&sb, "export ");
    sb_printf(&sb, "const %s = Object.freeze({\n", n->name);
    for (int i = 0; i < n->cases->count; i++) {
        Node *c = n->cases->items[i];
        if (c->params->count == 0) {
            sb_printf(&sb, "  %s: Object.freeze({ _case: \"%s\" }),\n",
                     c->name, c->name);
        } else {
            sb_printf(&sb, "  %s: (", c->name);
            for (int j = 0; j < c->params->count; j++) {
                if (j) sb_put(&sb, ", ");
                sb_printf(&sb, "_%d", j);
            }
            sb_printf(&sb, ") => Object.freeze({ _case: \"%s\"", c->name);
            for (int j = 0; j < c->params->count; j++) sb_printf(&sb, ", _%d", j);
            sb_put(&sb, " }),\n");
        }
    }
    sb_put(&sb, "});");
    return sb_take(&sb);
}

static char *gen_switch(Node *n, Ctx *ctx) {
    int my_sw = ++_sw_counter;
    char label[32]; snprintf(label, sizeof(label), "_sw%d", my_sw);
    char subj[32];  snprintf(subj, sizeof(subj), "_s%d", my_sw);

    Ctx sw = ctx_copy(ctx);
    ss_add(&sw.break_stack, "switch");
    sw.switch_label = xstrdup(label);

    StrBuf sb; sb_init(&sb);
    sb_printf(&sb, "%s: {\n", label);
    char *sub = gen_expr(n->a, ctx);
    sb_printf(&sb, "  const %s = %s;\n", subj, sub);
    free(sub);

    for (int i = 0; i < n->cases->count; i++) {
        Node *c = n->cases->items[i];
        StrBuf conds; sb_init(&conds);
        StrBuf binds; sb_init(&binds);
        SS bind_names = {0};

        for (int j = 0; j < c->patterns->count; j++) {
            char *bindings[64]; int bc = 0;
            char *cond = gen_pattern(c->patterns->items[j], subj, &sw, bindings, &bc);
            if (j) sb_put(&conds, " || ");
            sb_printf(&conds, "%s", cond);
            free(cond);
            for (int k = 0; k < bc; k++) {
                const char *ns = bindings[k];
                if (strncmp(ns, "const ", 6) == 0) ns += 6;
                const char *ne = ns;
                while (*ne && *ne != ' ' && *ne != '=') ne++;
                char namebuf[128];
                size_t nlen = (size_t)(ne - ns);
                if (nlen < sizeof(namebuf)) {
                    memcpy(namebuf, ns, nlen);
                    namebuf[nlen] = 0;
                    if (!ss_has(&bind_names, namebuf)) {
                        sb_put(&binds, bindings[k]);
                        sb_putc(&binds, ' ');
                        ss_add(&bind_names, namebuf);
                    }
                }
                free(bindings[k]);
            }
        }
        char *condstr = sb_take(&conds);
        char *bindstr = sb_take(&binds);

        if (c->a) {
            char *g = gen_expr(c->a, &sw);
            char *newcond = xmalloc(strlen(condstr) + strlen(bindstr) + strlen(g) + 128);
            sprintf(newcond, "(() => { if (!(%s)) return false; %s return (%s); })()",
                    condstr, bindstr, g);
            free(condstr); condstr = newcond;
            free(g);
        }

        sb_printf(&sb, "  if (%s) {\n", condstr);
        if (bindstr[0]) sb_printf(&sb, "    %s\n", bindstr);
        for (int j = 0; j < c->body->count; j++) {
            char *s = gen_stmt(c->body->items[j], &sw);
            sb_printf(&sb, "    %s\n", s);
            free(s);
        }
        sb_printf(&sb, "    break %s;\n  }\n", label);
        free(condstr); free(bindstr);
    }

    if (n->body) {
        sb_put(&sb, "  {\n");
        for (int j = 0; j < n->body->count; j++) {
            char *s = gen_stmt(n->body->items[j], &sw);
            sb_printf(&sb, "    %s\n", s);
            free(s);
        }
        sb_put(&sb, "  }\n");
    }
    sb_put(&sb, "}");
    return sb_take(&sb);
}

static char *gen_pattern(Node *p, const char *subj, Ctx *ctx, char **bindings, int *bc) {
    *bc = 0;
    StrBuf sb; sb_init(&sb);
    switch (p->kind) {
    case N_PAT_ENUM:
        sb_printf(&sb, "%s && %s._case === \"%s\"", subj, subj, p->name);
        for (int i = 0; i < p->bindings->count; i++) {
            Node *b = p->bindings->items[i];
            if (b->name) {
                char *s = xmalloc(256);
                snprintf(s, 256, "const %s = %s._%d;", b->name, subj, i);
                bindings[(*bc)++] = s;
            }
        }
        break;
    case N_PAT_BIND:
        if (p->name) {
            char *s = xmalloc(strlen(p->name) + strlen(subj) + 32);
            sprintf(s, "const %s = %s;", p->name, subj);
            bindings[(*bc)++] = s;
        }
        sb_put(&sb, "true");
        break;
    case N_PAT_WILD: sb_put(&sb, "true"); break;
    case N_PAT_LIT: {
        if (p->isnull) sb_printf(&sb, "%s === null", subj);
        else if (p->sval) { char *j = xstrdup(p->sval); sb_printf(&sb, "%s === \"%s\"", subj, j); free(j); }
        else if (p->is_bool) sb_printf(&sb, "%s === %s", subj, p->boolean ? "true" : "false");
        else sb_printf(&sb, "%s === %.17g", subj, p->num);
        break;
    }
    case N_PAT_RANGE: {
        char *lo = gen_expr(p->a, ctx);
        char *hi = gen_expr(p->b, ctx);
        sb_printf(&sb, "(%s >= %s) && (%s %s %s)", subj, lo, subj, p->inclusive ? "<=" : "<", hi);
        free(lo); free(hi);
        break;
    }
    case N_PAT_EXPR: {
        char *e = gen_expr(p->a, ctx);
        sb_printf(&sb, "%s === %s", subj, e);
        free(e);
        break;
    }
    default: sb_put(&sb, "true");
    }
    return sb_take(&sb);
}

static char *gen_struct(Node *n, Ctx *parent) {
    SS members = {0};
    for (int i = 0; i < n->fields->count; i++) ss_add(&members, ((Node*)n->fields->items[i])->name);
    for (int i = 0; i < n->methods->count; i++) ss_add(&members, ((Node*)n->methods->items[i])->name);

    StrBuf sb; sb_init(&sb);
    if (n->exported) sb_put(&sb, "export ");
    sb_printf(&sb, "function %s(_args) {\n", n->name);
    if (n->fields->count) {
        sb_put(&sb, "  const { ");
        for (int i = 0; i < n->fields->count; i++) {
            if (i) sb_put(&sb, ", ");
            sb_put(&sb, ((Node*)n->fields->items[i])->name);
        }
        sb_put(&sb, " } = _args;\n");
        sb_put(&sb, "  const _self = { ");
        for (int i = 0; i < n->fields->count; i++) {
            if (i) sb_put(&sb, ", ");
            sb_put(&sb, ((Node*)n->fields->items[i])->name);
        }
        sb_put(&sb, " };\n");
    } else sb_put(&sb, "  const _self = {};\n");

    for (int i = 0; i < n->methods->count; i++) {
        Node *m = n->methods->items[i];
        Ctx mctx = ctx_copy(parent);
        mctx.fields = members;
        mctx.self_prefix = "_self";
        SS loc = {0};
        for (int j = 0; j < m->params->count; j++) ss_add(&loc, ((Node*)m->params->items[j])->name);
        mctx.locals = loc;

        sb_printf(&sb, "  _self.%s = function(", m->name);
        for (int j = 0; j < m->params->count; j++) {
            if (j) sb_put(&sb, ", ");
            sb_put(&sb, ((Node*)m->params->items[j])->name);
        }
        sb_put(&sb, ") {\n");

        if (m->body->count) {
            for (int j = 0; j < m->body->count; j++) {
                Node *s = m->body->items[j];
                int is_last = (j == m->body->count - 1);
                if (is_last && s->kind == N_EXPR_STMT) {
                    char *e = gen_expr(s->a, &mctx);
                    sb_printf(&sb, "    return %s;\n", e);
                    free(e);
                } else {
                    char *c = gen_stmt(s, &mctx);
                    sb_printf(&sb, "    %s\n", c);
                    free(c);
                }
            }
        } else sb_put(&sb, "    return null;\n");
        sb_put(&sb, "  };\n");
    }
    int all_let = 1;
    for (int i = 0; i < n->fields->count; i++)
        if (strcmp(((Node*)n->fields->items[i])->sval, "let") != 0) all_let = 0;
    sb_put(&sb, all_let ? "  return Object.freeze(_self);\n" : "  return _self;\n");
    sb_put(&sb, "}");
    return sb_take(&sb);
}

static char *gen_class(Node *n, Ctx *parent) {
    SS members = {0};
    for (int i = 0; i < n->fields->count; i++)
        ss_add(&members, ((Node*)n->fields->items[i])->name);
    for (int i = 0; i < n->methods->count; i++)
        ss_add(&members, ((Node*)n->methods->items[i])->name);

    StrBuf sb; sb_init(&sb);
    if (n->exported) sb_put(&sb, "export ");
    sb_printf(&sb, "class %s", n->name);
    if (n->sval) sb_printf(&sb, " extends %s", n->sval);
    sb_put(&sb, " {\n");

    for (int i = 0; i < n->methods->count; i++) {
        Node *m = n->methods->items[i];
        Ctx mctx = ctx_copy(parent);
        mctx.fields = members;
        mctx.self_prefix = "this";
        SS loc = {0};
        for (int j = 0; j < m->params->count; j++)
            ss_add(&loc, ((Node*)m->params->items[j])->name);
        mctx.locals = loc;

        int is_init = (strcmp(m->name, "init") == 0);
        const char *mname = is_init ? "constructor" : m->name;
        sb_printf(&sb, "  %s(", mname);
        for (int j = 0; j < m->params->count; j++) {
            if (j) sb_put(&sb, ", ");
            sb_put(&sb, ((Node*)m->params->items[j])->name);
        }
        sb_put(&sb, ") {\n");

        if (is_init) {
            for (int fi = 0; fi < n->fields->count; fi++) {
                Node *fld = n->fields->items[fi];
                if (fld->a) {
                    char *v = gen_expr(fld->a, &mctx);
                    sb_printf(&sb, "    this.%s = %s;\n", fld->name, v);
                    free(v);
                }
            }
        }

        for (int j = 0; j < m->body->count; j++) {
            Node *s = m->body->items[j];
            int is_last = (j == m->body->count - 1);
            if (is_last && s->kind == N_EXPR_STMT && !is_init) {
                char *e = gen_expr(s->a, &mctx);
                sb_printf(&sb, "    return %s;\n", e);
                free(e);
            } else {
                char *c = gen_stmt(s, &mctx);
                sb_printf(&sb, "    %s\n", c);
                free(c);
            }
        }
        sb_put(&sb, "  }\n");
    }

    sb_put(&sb, "}");
    return sb_take(&sb);
}

/* ---------- expr ---------- */

static char *gen_expr(Node *n, Ctx *ctx) {
    StrBuf sb; sb_init(&sb);
    switch (n->kind) {
    case N_IF_LET:
    case N_GUARD_LET:
        return xstrdup("undefined /* if-let/guard as expression not supported */");
    case N_LIT:
        if (n->isnull) return xstrdup("null");
        if (n->sval) {
            sb_putc(&sb, '"');
            for (const char *s = n->sval; *s; s++) {
                if (*s == '"' || *s == '\\') { sb_putc(&sb, '\\'); sb_putc(&sb, *s); }
                else if (*s == '\n') sb_put(&sb, "\\n");
                else if (*s == '\t') sb_put(&sb, "\\t");
                else if (*s == '\r') sb_put(&sb, "\\r");
                else sb_putc(&sb, *s);
            }
            sb_putc(&sb, '"');
            return sb_take(&sb);
        }
        if (n->is_bool) return xstrdup(n->boolean ? "true" : "false");
        sb_printf(&sb, "%.17g", n->num);
        return sb_take(&sb);
    case N_ID: {
        if (n->name && strcmp(n->name, "self") == 0 && ctx->self_prefix)
            return xstrdup(ctx->self_prefix);
        if (ctx->fields.count && ss_has(&ctx->fields, n->name) &&
            !(ctx->locals.count && ss_has(&ctx->locals, n->name))) {
            const char *p = ctx->self_prefix ? ctx->self_prefix : "_self";
            sb_printf(&sb, "%s.%s", p, n->name);
            return sb_take(&sb);
        }
        return xstrdup(n->name);
    }
    case N_ENUM_CASE: {
        const char *en = enum_find(ctx, n->name);
        if (en) { sb_printf(&sb, "%s.%s", en, n->name); return sb_take(&sb); }
        return xstrdup(n->name);
    }
    case N_TEMPLATE: return gen_template(n, ctx);
    case N_ARRAY:
        sb_putc(&sb, '[');
        for (int i = 0; i < n->items->count; i++) {
            if (i) sb_put(&sb, ", ");
            char *e = gen_expr(n->items->items[i], ctx);
            sb_put(&sb, e); free(e);
        }
        sb_putc(&sb, ']');
        return sb_take(&sb);
    case N_DICT:
        sb_put(&sb, "({");
        for (int i = 0; i < n->pairs->count; i++) {
            if (i) sb_put(&sb, ", ");
            Node *pr = n->pairs->items[i];
            char *k = gen_expr(pr->a, ctx);
            char *v = gen_expr(pr->b, ctx);
            sb_printf(&sb, "[%s]: %s", k, v);
            free(k); free(v);
        }
        sb_put(&sb, "})");
        return sb_take(&sb);
    case N_TUPLE:
        sb_putc(&sb, '[');
        for (int i = 0; i < n->items->count; i++) {
            if (i) sb_put(&sb, ", ");
            char *e = gen_expr(n->items->items[i], ctx);
            sb_put(&sb, e); free(e);
        }
        sb_putc(&sb, ']');
        return sb_take(&sb);
    case N_MEMBER: {
        char *o = gen_expr(n->a, ctx);
        sb_printf(&sb, "%s%s%s", o, n->optional ? "?." : ".", n->name);
        free(o);
        return sb_take(&sb);
    }
    case N_INDEX: {
        char *o = gen_expr(n->a, ctx);
        char *i = gen_expr(n->b, ctx);
        sb_printf(&sb, "%s[%s]", o, i);
        free(o); free(i);
        return sb_take(&sb);
    }
    case N_CALL: return gen_call(n, ctx);
    case N_FORCE: {
        char *a = gen_expr(n->a, ctx);
        sb_printf(&sb, "_force(%s)", a); free(a);
        return sb_take(&sb);
    }
    case N_UNARY: {
        char *a = gen_expr(n->a, ctx);
        sb_printf(&sb, "(%s%s)", n->op, a); free(a);
        return sb_take(&sb);
    }
    case N_BIN: {
        char *L = gen_expr(n->a, ctx);
        char *R = gen_expr(n->b, ctx);
        if (strcmp(n->op, "..<") == 0)      sb_printf(&sb, "_range(%s, %s)", L, R);
        else if (strcmp(n->op, "...") == 0) sb_printf(&sb, "_rangeIn(%s, %s)", L, R);
        else if (strcmp(n->op, "??") == 0)  sb_printf(&sb, "(%s ?? %s)", L, R);
        else                                sb_printf(&sb, "(%s %s %s)", L, n->op, R);
        free(L); free(R);
        return sb_take(&sb);
    }
    case N_TERNARY: {
        char *c = gen_expr(n->a, ctx);
        char *t = gen_expr(n->b, ctx);
        char *e = gen_expr(n->c, ctx);
        sb_printf(&sb, "(%s ? %s : %s)", c, t, e);
        free(c); free(t); free(e);
        return sb_take(&sb);
    }
    case N_ASSIGN: {
        char *t = gen_expr(n->a, ctx);
        char *v = gen_expr(n->b, ctx);
        sb_printf(&sb, "(%s %s %s)", t, n->op, v);
        free(t); free(v);
        return sb_take(&sb);
    }
    case N_IF: return gen_if_expr(n, ctx);
    case N_SWITCH: {
        char *s = gen_switch(n, ctx);
        sb_printf(&sb, "(() => { %s })()", s);
        free(s);
        return sb_take(&sb);
    }
    case N_CLOSURE: return gen_closure(n, ctx);
    case N_AWAIT: {
        char *a = gen_expr(n->a, ctx);
        sb_printf(&sb, "(await %s)", a); free(a);
        return sb_take(&sb);
    }
    default:
        codegen_error(n->line, n->col, "codegen: unknown expression kind %d", n->kind);
        sb_printf(&sb, "/* codegen error */");
        return sb_take(&sb);
    }
}

static char *gen_call(Node *n, Ctx *ctx) {
    if (n->a->kind == N_MEMBER && n->a->a && n->a->a->kind == N_ID &&
        n->a->a->name && strcmp(n->a->a->name, "super") == 0 &&
        n->a->name && strcmp(n->a->name, "init") == 0) {
        StrBuf sb; sb_init(&sb);
        sb_put(&sb, "super(");
        for (int i = 0; i < n->args->count; i++) {
            if (i) sb_put(&sb, ", ");
            char *v = gen_expr(((Node*)n->args->items[i])->a, ctx);
            sb_put(&sb, v); free(v);
        }
        sb_putc(&sb, ')');
        return sb_take(&sb);
    }

    if (n->a->kind == N_ID && ss_has(&ctx->struct_names, n->a->name)) {
        StrBuf sb; sb_init(&sb);
        sb_printf(&sb, "%s({", n->a->name);
        for (int i = 0; i < n->args->count; i++) {
            Node *a = n->args->items[i];
            if (i) sb_put(&sb, ", ");
            if (!a->name) {
                codegen_error(n->a->line, n->a->col,
                    "struct %s: named arguments required in constructor",
                    n->a->name);
                sb_put(&sb, "_invalid_: ");
            } else {
                sb_printf(&sb, "%s: ", a->name);
            }
            char *v = gen_expr(a->a, ctx);
            sb_put(&sb, v); free(v);
        }
        sb_put(&sb, "})");
        return sb_take(&sb);
    }

    int do_swap = 0;
    if (n->a->kind == N_MEMBER && strcmp(n->a->name, "reduce") == 0 &&
        n->args->count == 2 && ((Node*)n->args->items[1])->a->kind == N_CLOSURE)
        do_swap = 1;

    StrBuf sb; sb_init(&sb);
    if (n->is_new) sb_put(&sb, "new ");
    char *callee = gen_expr(n->a, ctx);
    sb_put(&sb, callee); free(callee);
    if (n->optional) sb_put(&sb, "?.");
    sb_putc(&sb, '(');

    if (do_swap) {
        char *c = gen_expr(((Node*)n->args->items[1])->a, ctx);
        char *v = gen_expr(((Node*)n->args->items[0])->a, ctx);
        sb_printf(&sb, "%s, %s", c, v);
        free(c); free(v);
    } else {
        for (int i = 0; i < n->args->count; i++) {
            if (i) sb_put(&sb, ", ");
            char *v = gen_expr(((Node*)n->args->items[i])->a, ctx);
            sb_put(&sb, v); free(v);
        }
    }
    sb_putc(&sb, ')');
    return sb_take(&sb);
}

static char *gen_if_expr(Node *n, Ctx *ctx) {
    StrBuf sb; sb_init(&sb);
    char *cond = gen_expr(n->a, ctx);
    char *then_code = block_returning(n->b->stmts, ctx);
    sb_printf(&sb, "(() => { if (%s) { %s } ", cond, then_code);
    free(cond); free(then_code);
    if (!n->c) sb_put(&sb, "return null; ");
    else if (n->c->kind == N_IF) {
        char *e = gen_if_expr(n->c, ctx);
        sb_printf(&sb, "return %s; ", e);
        free(e);
    } else {
        char *e = block_returning(n->c->stmts, ctx);
        sb_printf(&sb, "{ %s } ", e);
        free(e);
    }
    sb_put(&sb, "})()");
    return sb_take(&sb);
}

static void collect_dollar(Node *n, int *max_idx, int *found) {
    if (!n) return;
    if (n->kind == N_CLOSURE) return;
    if (n->kind == N_ID && n->name && n->name[0] == '$') {
        int idx = atoi(n->name + 1);
        if (idx > *max_idx) *max_idx = idx;
        *found = 1;
    }
    collect_dollar(n->a, max_idx, found);
    collect_dollar(n->b, max_idx, found);
    collect_dollar(n->c, max_idx, found);
    collect_dollar(n->type, max_idx, found);
    collect_dollar(n->retType, max_idx, found);
    #define W(l) do { if (n->l) for (int i = 0; i < n->l->count; i++) collect_dollar((Node*)n->l->items[i], max_idx, found); } while(0)
    W(stmts); W(params); W(fields); W(methods); W(cases); W(body);
    W(args); W(items); W(pairs); W(patterns); W(bindings); W(parts);
    #undef W
}

static char *gen_closure(Node *n, Ctx *ctx) {
    StrBuf sb; sb_init(&sb);
    StrBuf params; sb_init(&params);
    if (n->params && n->params->count) {
        for (int i = 0; i < n->params->count; i++) {
            if (i) sb_put(&params, ", ");
            sb_put(&params, ((Node*)n->params->items[i])->name);
        }
    } else {
        int max_idx = -1, found = 0;
        if (n->body) {
            for (int i = 0; i < n->body->count; i++)
                collect_dollar((Node*)n->body->items[i], &max_idx, &found);
        }
        if (found) {
            for (int i = 0; i <= max_idx; i++) {
                if (i) sb_put(&params, ", ");
                sb_printf(&params, "$%d", i);
            }
        }
    }
    Ctx inner = ctx_copy(ctx);
    if (n->params) for (int i = 0; i < n->params->count; i++)
        ss_add(&inner.locals, ((Node*)n->params->items[i])->name);

    char *body = block_returning(n->body, &inner);
    char *pstr = sb_take(&params);
    if (n->is_async) sb_printf(&sb, "(async (%s) => { %s })", pstr, body);
    else             sb_printf(&sb, "((%s) => { %s })", pstr, body);
    free(pstr); free(body);
    return sb_take(&sb);
}

static char *gen_template(Node *n, Ctx *ctx) {
    StrBuf sb; sb_init(&sb);
    sb_putc(&sb, '`');
    for (int i = 0; i < n->parts->count; i++) {
        Node *p = n->parts->items[i];
        if (p->sval && strcmp(p->sval, "text") == 0) {
            for (const char *s = p->name; *s; s++) {
                if (*s == '\\') sb_put(&sb, "\\\\");
                else if (*s == '`') sb_put(&sb, "\\`");
                else if (*s == '$') sb_put(&sb, "\\$");
                else sb_putc(&sb, *s);
            }
        } else {
            char *e = gen_expr(p->a, ctx);
            sb_printf(&sb, "${String(%s)}", e);
            free(e);
        }
    }
    sb_putc(&sb, '`');
    return sb_take(&sb);
}

char *gen_program(Node *prog) {
    _sw_counter = 0;
    _let_bind_counter = 0;
    memset(&_gen, 0, sizeof(_gen));

    Ctx ctx; memset(&ctx, 0, sizeof(ctx));
    collect_decls(prog, &ctx);
    StrBuf sb; sb_init(&sb);
    for (int i = 0; i < prog->stmts->count; i++) {
        if (i) sb_putc(&sb, '\n');
        char *s = gen_stmt(prog->stmts->items[i], &ctx);
        sb_put(&sb, s); free(s);
    }
    if (_gen.has_err) {
        sb_free(&sb);
        return NULL;
    }
    return sb_take(&sb);
}