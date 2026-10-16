#include "codegen.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int _sw_counter = 0;

typedef struct { char **items; int count, cap; } SS;
static void ss_add(SS *s, const char *v) {
    for (int i = 0; i < s->count; i++) if (strcmp(s->items[i], v) == 0) return;
    if (s->count == s->cap) { s->cap = s->cap ? s->cap*2 : 8; s->items = xrealloc(s->items, sizeof(char*)*s->cap); }
    s->items[s->count++] = xstrdup(v);
}
static int ss_has(SS *s, const char *v) {
    if (!s) return 0;
    for (int i = 0; i < s->count; i++) if (strcmp(s->items[i], v) == 0) return 1;
    return 0;
}

typedef struct {
    SS struct_names;
    /* enumCases: (case_name → enum_name) */
    struct Pair { char *k; char *v; } *enum_cases;
    int enum_case_count, enum_case_cap;
    SS fields;
    int in_method;
    SS locals;
    SS break_stack;
    const char *switch_label;
} Ctx;

static void enum_add(Ctx *c, const char *case_name, const char *enum_name) {
    if (c->enum_case_count == c->enum_case_cap) {
        c->enum_case_cap = c->enum_case_cap ? c->enum_case_cap*2 : 8;
        c->enum_cases = xrealloc(c->enum_cases, sizeof(*c->enum_cases)*c->enum_case_cap);
    }
    c->enum_cases[c->enum_case_count].k = xstrdup(case_name);
    c->enum_cases[c->enum_case_count].v = xstrdup(enum_name);
    c->enum_case_count++;
}
static const char *enum_find(Ctx *c, const char *case_name) {
    for (int i = 0; i < c->enum_case_count; i++)
        if (strcmp(c->enum_cases[i].k, case_name) == 0) return c->enum_cases[i].v;
    return NULL;
}

static Ctx ctx_copy(Ctx *src) {
    Ctx c = *src;
    return c;
}

/* forward */
static char *gen_stmt(Node *n, Ctx *ctx);
static char *gen_expr(Node *n, Ctx *ctx);

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
static char *gen_if_stmt(Node *n, Ctx *ctx);
static char *gen_switch(Node *n, Ctx *ctx);
static char *gen_struct(Node *n, Ctx *ctx);
static char *gen_enum(Node *n, Ctx *ctx);
static char *block_returning(List *stmts, Ctx *ctx);

static char *gen_stmt(Node *n, Ctx *ctx) {
    StrBuf sb; sb_init(&sb);
    switch (n->kind) {
    case N_LET: {
        const char *kw = (n->sval && strcmp(n->sval, "var") == 0) ? "let" : "const";
        char *v = n->a ? gen_expr(n->a, ctx) : xstrdup("undefined");
        if (ctx->locals.items) ss_add(&ctx->locals, n->name);
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
        inner.fields.count = 0;  /* сбросить */
        inner.in_method = 0;
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
        sb_printf(&sb, "function %s(%s) {\n%s\n}", n->name, pstr, bstr);
        free(pstr); free(bstr);
        break;
    }
    case N_STRUCT: return gen_struct(n, ctx);
    case N_ENUM:   return gen_enum(n, ctx);
    case N_RETURN:
        if (n->a) { char *e = gen_expr(n->a, ctx); sb_printf(&sb, "return %s;", e); free(e); }
        else sb_put(&sb, "return;");
        break;
    case N_EXPR_STMT:
        if (n->a->kind == N_IF) return gen_if_stmt(n->a, ctx);
        if (n->a->kind == N_SWITCH) return gen_switch(n->a, ctx);
        { char *e = gen_expr(n->a, ctx); sb_printf(&sb, "%s;", e); free(e); }
        break;
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
    case N_BREAK: {
        const char *top = ctx->break_stack.count ?
            ctx->break_stack.items[ctx->break_stack.count-1] : NULL;
        if (top && strcmp(top, "switch") == 0)
            sb_printf(&sb, "break %s;", ctx->switch_label);
        else sb_put(&sb, "break;");
        break;
    }
    case N_CONTINUE: sb_put(&sb, "continue;"); break;
    default:
        sb_printf(&sb, "/* ??? stmt kind=%d */", n->kind);
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

static char *gen_enum(Node *n, Ctx *ctx) {
    StrBuf sb; sb_init(&sb);
    sb_printf(&sb, "const %s = Object.freeze({\n", n->name);
    for (int i = 0; i < n->cases->count; i++) {
        Node *c = n->cases->items[i];
        if (c->params->count == 0) {
            sb_printf(&sb, "  %s: Object.freeze({ _case: \"%s\" }),\n",
                        c->name, c->name);
        } else {
            /* параметры _0, _1, ... */
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

static char *gen_pattern(Node *p, const char *subj, Ctx *ctx, char **bindings_out, int *bindings_count);

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
        for (int j = 0; j < c->patterns->count; j++) {
            char *bindings[64]; int bc = 0;
            char *cond = gen_pattern(c->patterns->items[j], subj, &sw, bindings, &bc);
            if (j) sb_put(&conds, " || ");
            sb_printf(&conds, "%s", cond);
            free(cond);
            for (int k = 0; k < bc; k++) { sb_put(&binds, bindings[k]); sb_putc(&binds, ' '); free(bindings[k]); }
        }
        char *condstr = sb_take(&conds);
        char *bindstr = sb_take(&binds);

        if (c->a) { /* guard */
            char *g = gen_expr(c->a, &sw);
            char *newcond = xmalloc(strlen(condstr) + strlen(bindstr) + strlen(g) + 128);
            sprintf(newcond, "(() => { if (!(%s)) return false; %s return (%s); })()",
                    condstr, bindstr, g);
            free(condstr); condstr = newcond;
            free(g);
        }

        sb_printf(&sb, "  if (%s) {\n", condstr);
        if (bindstr[0]) {
            /* bindings added per-pattern — emit them again */
            for (int j = 0; j < c->patterns->count; j++) {
                char *b2[64]; int bc2 = 0;
                char *cc = gen_pattern(c->patterns->items[j], subj, &sw, b2, &bc2);
                free(cc);
                for (int k = 0; k < bc2; k++) { sb_printf(&sb, "    %s\n", b2[k]); free(b2[k]); }
            }
        }
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
        else if (p->boolean) sb_printf(&sb, "%s === true", subj);
        else if (!p->boolean && p->num == 0 && !p->sval) sb_printf(&sb, "%s === false", subj);
        else sb_printf(&sb, "%s === %g", subj, p->num);
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
        mctx.in_method = 1;
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

/* ---------- expr ---------- */
static char *gen_call(Node *n, Ctx *ctx);
static char *gen_closure(Node *n, Ctx *ctx);
static char *gen_template(Node *n, Ctx *ctx);
static char *gen_if_expr(Node *n, Ctx *ctx);

static char *gen_expr(Node *n, Ctx *ctx) {
    StrBuf sb; sb_init(&sb);
    switch (n->kind) {
    case N_LIT:
        if (n->isnull) return xstrdup("null");
        if (n->sval) {
            /* JSON escape */
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
        sb_printf(&sb, "%g", n->num);
        return sb_take(&sb);
    case N_ID: {
        if (n->name && strcmp(n->name, "self") == 0 && ctx->in_method) return xstrdup("_self");
        if (ctx->fields.count && ss_has(&ctx->fields, n->name) &&
            !(ctx->locals.count && ss_has(&ctx->locals, n->name))) {
            sb_printf(&sb, "_self.%s", n->name);
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
    case N_TRY: return gen_expr(n->a, ctx);
    case N_AWAIT: {
        char *a = gen_expr(n->a, ctx);
        sb_printf(&sb, "(await %s)", a); free(a);
        return sb_take(&sb);
    }
    default:
        sb_printf(&sb, "/* ??? expr kind=%d */", n->kind);
        return sb_take(&sb);
    }
}

static char *gen_call(Node *n, Ctx *ctx) {
    /* Foo(x: 1, y: 2) → Foo({x: 1, y: 2}) */
    if (n->a->kind == N_ID && ss_has(&ctx->struct_names, n->a->name)) {
        StrBuf sb; sb_init(&sb);
        sb_printf(&sb, "%s({", n->a->name);
        for (int i = 0; i < n->args->count; i++) {
            Node *a = n->args->items[i];
            if (i) sb_put(&sb, ", ");
            if (!a->name) { sb_put(&sb, "??: "); }
            else sb_printf(&sb, "%s: ", a->name);
            char *v = gen_expr(a->a, ctx);
            sb_put(&sb, v); free(v);
        }
        sb_put(&sb, "})");
        return sb_take(&sb);
    }

    /* JS: args.length === 2 && args[1].k === "Closure" */
    int do_swap = 0;
    if (n->a->kind == N_MEMBER && strcmp(n->a->name, "reduce") == 0 &&
        n->args->count == 2 && ((Node*)n->args->items[1])->a->kind == N_CLOSURE)
        do_swap = 1;

    StrBuf sb; sb_init(&sb);
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
    #define W(l) do { if (n->l) for (int i=0;i<n->l->count;i++) collect_dollar((Node*)n->l->items[i], max_idx, found); } while(0)
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
    sb_printf(&sb, "((%s) => { %s })", pstr, body);
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
    Ctx ctx; memset(&ctx, 0, sizeof(ctx));
    collect_decls(prog, &ctx);
    StrBuf sb; sb_init(&sb);
    for (int i = 0; i < prog->stmts->count; i++) {
        if (i) sb_putc(&sb, '\n');
        char *s = gen_stmt(prog->stmts->items[i], &ctx);
        sb_put(&sb, s); free(s);
    }
    return sb_take(&sb);
}