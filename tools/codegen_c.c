/* ============================================================================
 * swsc C backend — EXPERIMENTAL
 *
 * Known failures (as of v0.5, 24/36 native tests pass):
 *   - 07_classes, 23_protocol      : inheritance not emulated
 *   - 09_switch_ranges             : N_PAT_RANGE in switch not handled
 *   - 13_closures                  : closures with capture unsupported
 *   - 14_collections, 18_destruct  : SEGFAULT in compiler (TODO: gdb)
 *   - 17_extension                 : extension methods not wired
 *   - 19_computed_props            : getters not emitted
 *   - 21_stdlib                    : relative imports unresolved
 *   - try_optional                 : setjmp wrapper missing
 *   - 16_defer                     : defer not run on early return
 *
 * This file is kept as a research artifact. See README.
 * ==========================================================================*/

#include "codegen_c.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static int _sw_counter = 0;
static int _let_bind_counter = 0;

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */
static int _err;
static int _err_line, _err_col;
static char _err_msg[512];

static void codegen_error(int line, int col, const char *fmt, ...) {
    if (_err) return;
    _err = 1; _err_line = line; _err_col = col;
    va_list ap; va_start(ap, fmt);
    vsnprintf(_err_msg, sizeof(_err_msg), fmt, ap);
    va_end(ap);
}
const char *gen_c_error_msg(void) { return _err_msg; }
int gen_c_error_line(void) { return _err_line; }
int gen_c_error_col(void) { return _err_col; }

/* ------------------------------------------------------------------ */
/* Tiny string-set                                                     */
/* ------------------------------------------------------------------ */
typedef struct { char **items; int count, cap; } SS;
static void ss_add(SS *s, const char *v) {
    if (!v) return;
    for (int i = 0; i < s->count; i++) if (!strcmp(s->items[i], v)) return;
    if (s->count == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 8;
        s->items = xrealloc(s->items, sizeof(char*)*s->cap);
    }
    s->items[s->count++] = xstrdup(v);
}
static int  ss_has(SS *s, const char *v) {
    if (!s || !v) return 0;
    for (int i = 0; i < s->count; i++) if (!strcmp(s->items[i], v)) return 1;
    return 0;
}
static void ss_free(SS *s) {
    if (!s) return;
    for (int i = 0; i < s->count; i++) free(s->items[i]);
    free(s->items); s->items=NULL; s->count=s->cap=0;
}
static SS ss_clone(SS *src) {
    SS c={0}; for (int i=0;i<src->count;i++) ss_add(&c, src->items[i]); return c;
}

/* ------------------------------------------------------------------ */
/* BindingList                                                         */
/* ------------------------------------------------------------------ */
typedef struct { char **items; int count, cap; } BindingList;
static void bl_push(BindingList *bl, char *s) {
    if (bl->count==bl->cap) {
        bl->cap = bl->cap ? bl->cap*2 : 8;
        bl->items = xrealloc(bl->items, sizeof(char*)*bl->cap);
    }
    bl->items[bl->count++] = s;
}
static void bl_clear(BindingList *bl) {
    for (int i=0;i<bl->count;i++) free(bl->items[i]);
    free(bl->items); bl->items=NULL; bl->count=bl->cap=0;
}

/* ------------------------------------------------------------------ */
/* EnumDef and Ctx                                                     */
/* ------------------------------------------------------------------ */
typedef struct { char *name; SS cases; } EnumDef;
typedef struct {
    SS struct_names, class_names, globals, func_names;
    struct Pair { char *k, *v; } *enum_cases;
    int enum_case_count, enum_case_cap;
    EnumDef *enums; int enum_count, enum_cap;
    SS fields;
    const char *self_prefix;
    SS locals, break_stack;
    char *switch_label;
    int  top_level;
} Ctx;

static void enum_add(Ctx *c, const char *k, const char *v) {
    if (c->enum_case_count == c->enum_case_cap) {
        c->enum_case_cap = c->enum_case_cap ? c->enum_case_cap*2 : 8;
        c->enum_cases = xrealloc(c->enum_cases, sizeof(*c->enum_cases)*c->enum_case_cap);
    }
    c->enum_cases[c->enum_case_count].k = xstrdup(k);
    c->enum_cases[c->enum_case_count].v = xstrdup(v);
    c->enum_case_count++;
}
static const char *enum_find(Ctx *c, const char *k) {
    if (!k) return NULL;
    for (int i=c->enum_case_count-1;i>=0;i--)
        if (!strcmp(c->enum_cases[i].k, k)) return c->enum_cases[i].v;
    return NULL;
}

static Ctx ctx_copy(Ctx *src) {
    Ctx c = *src;
    c.locals      = ss_clone(&src->locals);
    c.break_stack = ss_clone(&src->break_stack);
    c.fields      = ss_clone(&src->fields);
    return c;
}
static void ctx_destroy_no_fields(Ctx *c) { ss_free(&c->locals); ss_free(&c->break_stack); }
static void ctx_destroy(Ctx *c)          { ctx_destroy_no_fields(c); ss_free(&c->fields); }

/* ------------------------------------------------------------------ */
/* Forward decls                                                       */
/* ------------------------------------------------------------------ */
static char *gen_stmt(Node *n, Ctx *ctx);
static char *gen_expr(Node *n, Ctx *ctx);
static char *gen_import(Node *n);
static char *gen_class(Node *n, Ctx *ctx);
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
static char *gen_pattern(Node *p, const char *subj, Ctx *ctx, BindingList *bindings);
static char *gen_if_case(Node *n, Ctx *ctx);
static char *gen_guard_case(Node *n, Ctx *ctx);

/* ------------------------------------------------------------------ */
/* collect_decls                                                       */
/* ------------------------------------------------------------------ */
static void collect_decls(Node *prog, Ctx *ctx) {
    for (int i=0;i<prog->stmts->count;i++) {
        Node *s = prog->stmts->items[i];
        if (s->name) ss_add(&ctx->globals, s->name);
        if (s->kind == N_FUNC && s->name) ss_add(&ctx->func_names, s->name);
        if (s->kind == N_IMPORT && s->items) {
            for (int j=0;j<s->items->count;j++) {
                Node *spec = s->items->items[j];
                const char *name = spec->sval ? spec->sval : spec->name;
                if (name) ss_add(&ctx->globals, name);
            }
        }
        if (s->kind == N_PROTOCOL && s->name) ss_add(&ctx->globals, s->name);
        if (s->kind == N_STRUCT && s->name) ss_add(&ctx->struct_names, s->name);
        if (s->kind == N_CLASS  && s->name) ss_add(&ctx->class_names,  s->name);
        if (s->kind == N_ENUM && s->name && s->cases) {
            EnumDef ed={0}; ed.name = xstrdup(s->name);
            for (int j=0;j<s->cases->count;j++) {
                Node *c = s->cases->items[j];
                if (!c || !c->name) continue;
                ss_add(&ed.cases, c->name);
                enum_add(ctx, c->name, s->name);
            }
            if (ctx->enum_count == ctx->enum_cap) {
                ctx->enum_cap = ctx->enum_cap ? ctx->enum_cap*2 : 4;
                ctx->enums = xrealloc(ctx->enums, sizeof(EnumDef)*ctx->enum_cap);
            }
            ctx->enums[ctx->enum_count++] = ed;
        }
    }
    /* extension merging */
    for (int i=0;i<prog->stmts->count;i++) {
        Node *ext = prog->stmts->items[i];
        if (ext->kind != N_EXTENSION) continue;
        Node *target = NULL;
        for (int j=0;j<prog->stmts->count;j++) {
            Node *d = prog->stmts->items[j];
            if ((d->kind==N_STRUCT || d->kind==N_CLASS) && d->name &&
                !strcmp(d->name, ext->name)) { target = d; break; }
        }
        if (!target) { codegen_error(ext->line, ext->col,
                       "Cannot extend unknown type '%s'", ext->name); continue; }
        for (int k=0;k<ext->methods->count;k++) {
            Node *m = ext->methods->items[k];
            int dup=0;
            for (int q=0;q<target->methods->count;q++)
                if (!strcmp(((Node*)target->methods->items[q])->name, m->name)) { dup=1; break; }
            if (!dup) list_push(target->methods, m);
            else codegen_error(m->line, m->col, "Duplicate method '%s'", m->name);
        }
        ext->methods->count = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */
static int is_global(Ctx *ctx, const char *name) {
    return ss_has(&ctx->globals, name) && !ss_has(&ctx->locals, name);
}
static void emit_name(StrBuf *sb, Ctx *ctx, const char *name) {
    if (is_global(ctx, name)) sb_printf(sb, "g_%s", name);
    else                      sb_put(sb, name);
}

static void emit_c_string(StrBuf *sb, const char *s) {
    sb_putc(sb, '"');
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':  sb_put(sb, "\\\""); break;
        case '\\': sb_put(sb, "\\\\"); break;
        case '\n': sb_put(sb, "\\n");  break;
        case '\r': sb_put(sb, "\\r");  break;
        case '\t': sb_put(sb, "\\t");  break;
        case '\0': break;
        default:
            if (c < 0x20) sb_printf(sb, "\\x%02x", c);
            else          sb_putc(sb, (char)c);
        }
    }
    sb_putc(sb, '"');
}

/* ------------------------------------------------------------------ */
/* gen_stmts_with_defers                                               */
/* ------------------------------------------------------------------ */
static void gen_stmts_with_defers(StrBuf *sb, List *stmts, Ctx *ctx,
                                  const char *indent, int implicit_return) {
    List *defers  = list_new();
    List *regular = list_new();
    for (int i=0;i<stmts->count;i++) {
        Node *s = stmts->items[i];
        if (s->kind == N_DEFER) list_push(defers, s);
        else                    list_push(regular, s);
    }

    if (defers->count == 0) {
        for (int i=0;i<regular->count;i++) {
            Node *s = regular->items[i];
            int is_last = (i == regular->count-1);
            if (implicit_return && is_last && s->kind == N_EXPR_STMT && s->a
                && s->a->kind != N_IF && s->a->kind != N_IF_LET
                && s->a->kind != N_SWITCH && s->a->kind != N_IF_CASE) {
                char *e = gen_expr(s->a, ctx);
                sb_printf(sb, "%sreturn %s;\n", indent, e);
                free(e);
            } else {
                char *code = gen_stmt(s, ctx);
                sb_printf(sb, "%s%s\n", indent, code);
                free(code);
            }
        }
        if (implicit_return && (regular->count == 0 ||
            !(regular->items[regular->count-1] &&
              ((Node*)regular->items[regular->count-1])->kind == N_RETURN)))
            sb_printf(sb, "%sreturn sws_undef();\n", indent);
    } else {
        sb_printf(sb, "%s{\n", indent);
        for (int i=0;i<regular->count;i++) {
            Node *s = regular->items[i];
            int is_last = (i == regular->count-1);
            if (implicit_return && is_last && s->kind == N_EXPR_STMT && s->a
                && s->a->kind != N_IF && s->a->kind != N_IF_LET
                && s->a->kind != N_SWITCH && s->a->kind != N_IF_CASE) {
                char *e = gen_expr(s->a, ctx);
                sb_printf(sb, "%s  return %s;\n", indent, e);
                free(e);
            } else {
                char *code = gen_stmt(s, ctx);
                sb_printf(sb, "%s  %s\n", indent, code);
                free(code);
            }
        }
        for (int i=defers->count-1;i>=0;i--) {
            Node *d = defers->items[i];
            for (int j=0;j<d->a->stmts->count;j++) {
                char *code = gen_stmt(d->a->stmts->items[j], ctx);
                sb_printf(sb, "%s  %s\n", indent, code);
                free(code);
            }
        }
        sb_printf(sb, "%s}\n", indent);
    }
    free(defers->items); free(regular->items);
}

/* ------------------------------------------------------------------ */
/* gen_stmt                                                            */
/* ------------------------------------------------------------------ */
static char *gen_stmt(Node *n, Ctx *ctx) {
    StrBuf sb; sb_init(&sb);
    switch (n->kind) {

    case N_IMPORT: return gen_import(n);

    case N_LET: {
        char *v = n->a ? gen_expr(n->a, ctx) : xstrdup("sws_undef()");

        if (n->items && n->items->count > 0) {
            if (ctx->top_level) {
                sb_printf(&sb, "SwsVal _dtmp = %s;\n", v);
                for (int i=0;i<n->items->count;i++) {
                    const char *name = n->items->items[i];
                    sb_printf(&sb, "g_%s = sws_index(_dtmp, sws_num(%d));\n", name, i);
                }
            } else {
                sb_printf(&sb, "SwsVal _dtmp = %s;\n", v);
                for (int i=0;i<n->items->count;i++) {
                    const char *name = n->items->items[i];
                    ss_add(&ctx->locals, name);
                    sb_printf(&sb, "SwsVal %s = sws_index(_dtmp, sws_num(%d));\n", name, i);
                }
            }
        } else if (ctx->top_level && is_global(ctx, n->name)) {
            sb_printf(&sb, "g_%s = %s;", n->name, v);
        } else {
            if (ss_has(&ctx->locals, n->name))
                codegen_error(n->line, n->col, "Duplicate declaration of '%s'", n->name);
            ss_add(&ctx->locals, n->name);
            sb_printf(&sb, "SwsVal %s = %s;", n->name, v);
        }
        free(v);
        break;
    }

    case N_FUNC: {
        StrBuf ps; sb_init(&ps);
        for (int i=0;i<n->params->count;i++) {
            if (i) sb_put(&ps, ", ");
            sb_printf(&ps, "SwsVal %s", ((Node*)n->params->items[i])->name);
        }
        Ctx inner = ctx_copy(ctx);
        ss_free(&inner.fields); inner.self_prefix = NULL;
        ss_free(&inner.locals); inner.locals = (SS){0};
        for (int i=0;i<n->params->count;i++)
            ss_add(&inner.locals, ((Node*)n->params->items[i])->name);

        StrBuf body; sb_init(&body);
        gen_stmts_with_defers(&body, n->body, &inner, "  ", 1);
        char *pstr = sb_take(&ps);
        char *bstr = sb_take(&body);
        sb_printf(&sb, "static SwsVal fn_%s(%s) {\n%s}\n", n->name, pstr, bstr);
        free(pstr); free(bstr);
        ctx_destroy(&inner);
        break;
    }

    case N_EXTENSION: case N_DEFER: case N_PROTOCOL:
        return xstrdup("/* erased */");

    case N_STRUCT: return gen_struct(n, ctx);
    case N_ENUM:   return gen_enum(n, ctx);
    case N_CLASS:  return gen_class(n, ctx);

    case N_RETURN:
        if (n->a) { char *e = gen_expr(n->a, ctx); sb_printf(&sb, "return %s;", e); free(e); }
        else sb_put(&sb, "return sws_undef();");
        break;

    case N_EXPR_STMT:
        if (!n->a) { codegen_error(n->line, n->col, "empty expression statement"); break; }
        if (n->a->kind == N_IF)      return gen_stmt(n->a, ctx);
        if (n->a->kind == N_IF_LET)  return gen_if_let_stmt(n->a, ctx);
        if (n->a->kind == N_IF_CASE) return gen_if_case(n->a, ctx);
        if (n->a->kind == N_SWITCH)  return gen_switch(n->a, ctx);
        { char *e = gen_expr(n->a, ctx);
          sb_printf(&sb, "(void)(%s);", e); free(e); }
        break;

    case N_GUARD_LET:  return gen_guard_stmt(n, ctx);
    case N_GUARD_CASE: return gen_guard_case(n, ctx);

    case N_BLOCK: {
        sb_put(&sb, "{\n");
        Ctx bc = ctx_copy(ctx);
        gen_stmts_with_defers(&sb, n->stmts, &bc, "  ", 0);
        ctx_destroy(&bc);
        sb_put(&sb, "}");
        break;
    }

    case N_IF: {
        char *c = gen_expr(n->a, ctx);
        char *t = gen_stmt(n->b, ctx);
        sb_printf(&sb, "if (sws_truthy(%s)) %s", c, t);
        free(c); free(t);
        if (n->c) {
            if (n->c->kind == N_IF) {
                StrBuf el; sb_init(&el);
                char *ec = gen_expr(n->c->a, ctx);
                char *et = gen_stmt(n->c->b, ctx);
                sb_printf(&el, "if (sws_truthy(%s)) %s", ec, et);
                free(ec); free(et);
                if (n->c->c) {
                    char *ee = gen_stmt(n->c->c, ctx);
                    sb_printf(&el, " else %s", ee); free(ee);
                }
                sb_printf(&sb, " else %s", el.buf);
                sb_free(&el);
            } else {
                char *e = gen_stmt(n->c, ctx);
                sb_printf(&sb, " else %s", e); free(e);
            }
        }
        break;
    }

    case N_WHILE: {
        Ctx inner = ctx_copy(ctx);
        ss_add(&inner.break_stack, "loop");
        char *c = gen_expr(n->a, ctx);
        char *b = gen_stmt(n->b, &inner);
        sb_printf(&sb, "while (sws_truthy(%s)) %s", c, b);
        free(c); free(b);
        ctx_destroy(&inner);
        break;
    }

    case N_FOR: {
        Ctx inner = ctx_copy(ctx);
        ss_add(&inner.break_stack, "loop");
        char *src = gen_expr(n->a, ctx);

        if (n->items && n->items->count > 0) {
            sb_put(&sb, "{\n  SwsVal _iter = ");
            sb_put(&sb, src);
            sb_put(&sb, ";\n  SwsVal _arr = sws_as_array(_iter);\n");
            sb_put(&sb, "  for (size_t _i = 0; _i < _arr.arr->len; _i++) {\n");
            sb_put(&sb, "    SwsVal _item = _arr.arr->items[_i];\n");
            for (int i=0;i<n->items->count;i++) {
                const char *name = n->items->items[i];
                ss_add(&inner.locals, name);
                sb_printf(&sb, "    SwsVal %s = sws_index(_item, sws_num(%d));\n", name, i);
            }
            char *b = gen_stmt(n->b, &inner);
            sb_printf(&sb, "    %s\n  }\n}", b);
            free(b);
        } else {
            ss_add(&inner.locals, n->name);
            sb_put(&sb, "{\n  SwsVal _iter = ");
            sb_put(&sb, src);
            sb_put(&sb, ";\n  SwsVal _arr = sws_as_array(_iter);\n");
            sb_put(&sb, "  for (size_t _i = 0; _i < _arr.arr->len; _i++) {\n");
            sb_printf(&sb, "    SwsVal %s = _arr.arr->items[_i];\n", n->name);
            char *b = gen_stmt(n->b, &inner);
            sb_printf(&sb, "    %s\n  }\n}", b);
            free(b);
        }
        free(src);
        ctx_destroy(&inner);
        break;
    }

    case N_TRY: {
        char *body = gen_stmt(n->a, ctx);
        Ctx hc = ctx_copy(ctx);
        const char *cvar = n->name ? n->name : "_e";
        ss_add(&hc.locals, cvar);
        char *handler = gen_stmt(n->b, &hc);
        sb_printf(&sb,
            "{\n"
            "  SwsTryFrame _tf;\n"
            "  _tf.prev = _sws_try_top; _sws_try_top = &_tf;\n"
            "  if (setjmp(_tf.buf) == 0) {\n"
            "    %s\n"
            "    _sws_try_top = _tf.prev;\n"
            "  } else {\n"
            "    _sws_try_top = _tf.prev;\n"
            "    SwsVal %s = _sws_thrown;\n"
            "    %s\n"
            "  }\n"
            "}", body, cvar, handler);
        free(body); free(handler);
        ctx_destroy(&hc);
        break;
    }

    case N_THROW:
        if (n->a) { char *e = gen_expr(n->a, ctx);
                    sb_printf(&sb, "sws_throw(%s);", e); free(e); }
        else       sb_put(&sb, "sws_rethrow();");
        break;

    case N_BREAK: sb_put(&sb, "break;"); break;
    case N_CONTINUE: sb_put(&sb, "continue;"); break;

    default:
        codegen_error(n->line, n->col, "codegen: unknown stmt kind %d", n->kind);
        sb_put(&sb, "/* err */");
    }
    return sb_take(&sb);
}

/* ------------------------------------------------------------------ */
/* gen_if_case, gen_guard_case                                         */
/* ------------------------------------------------------------------ */
static char *gen_if_case(Node *n, Ctx *ctx) {
    int id = ++_sw_counter;
    char subj[32]; snprintf(subj, sizeof(subj), "_cs%d", id);

    StrBuf sb; sb_init(&sb);
    sb_printf(&sb, "{\n  SwsVal %s = ", subj);
    char *se = gen_expr(n->b, ctx); sb_printf(&sb, "%s;\n", se); free(se);

    BindingList bl = {0};
    char *cond = gen_pattern(n->a, subj, ctx, &bl);
    sb_printf(&sb, "  if (%s) {\n", cond);
    for (int i=0;i<bl.count;i++) sb_printf(&sb, "    %s\n", bl.items[i]);

    Ctx inner = ctx_copy(ctx);
    for (int i=0;i<bl.count;i++) {
        const char *ns = bl.items[i];
        if (!strncmp(ns,"SwsVal ",7)) ns += 7;
        const char *ne = ns;
        while (*ne && *ne!=' ' && *ne!='=') ne++;
        char *nb = xstrndup(ns, (size_t)(ne-ns));
        ss_add(&inner.locals, nb); free(nb);
    }
    char *body = gen_stmt(n->c, &inner);
    sb_printf(&sb, "    %s\n  }\n}", body);
    free(cond); free(body);
    bl_clear(&bl); ctx_destroy(&inner);
    return sb_take(&sb);
}

static char *gen_guard_case(Node *n, Ctx *ctx) {
    int id = ++_sw_counter;
    char subj[32]; snprintf(subj, sizeof(subj), "_gs%d", id);
    StrBuf sb; sb_init(&sb);
    sb_printf(&sb, "SwsVal %s = ", subj);
    char *se = gen_expr(n->b, ctx); sb_printf(&sb, "%s;\n", se); free(se);

    BindingList bl = {0};
    char *cond = gen_pattern(n->a, subj, ctx, &bl);
    sb_printf(&sb, "if (!(%s)) ", cond);
    char *else_code = gen_stmt(n->c, ctx);
    sb_printf(&sb, "%s\n", else_code);
    for (int i=0;i<bl.count;i++) {
        const char *ns = bl.items[i];
        if (!strncmp(ns,"SwsVal ",7)) ns += 7;
        const char *ne = ns;
        while (*ne && *ne!=' ' && *ne!='=') ne++;
        char *nb = xstrndup(ns, (size_t)(ne-ns));
        sb_printf(&sb, "%s\n", bl.items[i]);
        ss_add(&ctx->locals, nb); free(nb);
    }
    free(cond); free(else_code);
    bl_clear(&bl);
    return sb_take(&sb);
}

/* ------------------------------------------------------------------ */
/* gen_if_let_stmt                                                     */
/* ------------------------------------------------------------------ */
static char *gen_if_let_stmt(Node *n, Ctx *ctx) {
    StrBuf sb; sb_init(&sb);
    int id = ++_let_bind_counter;
    int count = n->items->count;
    int has_else = (n->c != NULL);
    Ctx body = ctx_copy(ctx);

    sb_put(&sb, "{\n");
    if (has_else) sb_printf(&sb, "  int _ok%d = 0;\n", id);

    for (int i=0;i<count;i++) {
        Node *b = n->items->items[i];
        char *v = gen_expr(b->a, ctx);
        sb_printf(&sb, "  SwsVal _t%d_%d = %s;\n", id, i, v);
        sb_printf(&sb, "  if (!sws_is_nullish(_t%d_%d)) {\n", id, i);
        if (b->items && b->items->count > 0) {
            for (int k=0;k<b->items->count;k++) {
                const char *name = b->items->items[k];
                ss_add(&body.locals, name);
                sb_printf(&sb, "    SwsVal %s = sws_index(_t%d_%d, sws_num(%d));\n",
                          name, id, i, k);
            }
        } else {
            sb_printf(&sb, "    SwsVal %s = _t%d_%d;\n", b->name, id, i);
            ss_add(&body.locals, b->name);
        }
        free(v);
    }

    if (n->a) {
        char *c = gen_expr(n->a, &body);
        sb_printf(&sb, "  if (sws_truthy(%s)) {\n", c); free(c);
    }
    if (has_else) sb_printf(&sb, "    _ok%d = 1;\n", id);

    if (n->b && n->b->kind == N_BLOCK) {
        for (int i=0;i<n->b->stmts->count;i++) {
            char *s = gen_stmt(n->b->stmts->items[i], &body);
            sb_printf(&sb, "    %s\n", s); free(s);
        }
    }
    if (n->a) sb_put(&sb, "  }\n");
    for (int i=0;i<count;i++) sb_put(&sb, "  }\n");
    if (has_else) {
        sb_printf(&sb, "  if (!_ok%d) ", id);
        char *e = gen_stmt(n->c, ctx);
        sb_printf(&sb, "%s\n", e); free(e);
    }
    sb_put(&sb, "}");
    ctx_destroy(&body);
    return sb_take(&sb);
}

/* ------------------------------------------------------------------ */
/* gen_guard_stmt                                                      */
/* ------------------------------------------------------------------ */
static char *gen_guard_stmt(Node *n, Ctx *ctx) {
    if (n->b && n->b->kind == N_BLOCK && !block_terminates(n->b->stmts))
        codegen_error(n->line, n->col,
            "guard else-block must end with return, throw, break, or continue");

    StrBuf sb; sb_init(&sb);
    int id = ++_let_bind_counter;
    int count = n->items->count;
    char *else_code = gen_stmt(n->b, ctx);

    if (count > 0) {
        for (int i=0;i<count;i++) {
            Node *b = n->items->items[i];
            char *v = gen_expr(b->a, ctx);
            sb_printf(&sb, "SwsVal _g%d_%d = %s;\n", id, i, v);
            sb_printf(&sb, "if (sws_is_nullish(_g%d_%d)) %s\n", id, i, else_code);
            free(v);
        }
        for (int i=0;i<count;i++) {
            Node *b = n->items->items[i];
            sb_printf(&sb, "SwsVal %s = _g%d_%d;\n", b->name, id, i);
            ss_add(&ctx->locals, b->name);
        }
    }
    if (n->a) {
        char *c = gen_expr(n->a, ctx);
        sb_printf(&sb, "if (!sws_truthy(%s)) %s\n", c, else_code);
        free(c);
    }
    free(else_code);
    return sb_take(&sb);
}

/* ------------------------------------------------------------------ */
/* block_terminates                                                    */
/* ------------------------------------------------------------------ */
static int block_terminates(List *stmts) {
    if (!stmts || !stmts->count) return 0;
    Node *last = stmts->items[stmts->count-1];
    switch (last->kind) {
    case N_RETURN: case N_THROW: case N_BREAK: case N_CONTINUE: return 1;
    case N_EXPR_STMT: {
        if (!last->a) return 0;
        Node *e = last->a;
        if (e->kind == N_IF) {
            if (!e->c || !e->b || e->b->kind != N_BLOCK) return 0;
            if (!block_terminates(e->b->stmts)) return 0;
            if (e->c->kind == N_IF) return 0;
            if (e->c->kind != N_BLOCK) return 0;
            return block_terminates(e->c->stmts);
        }
        if (e->kind == N_SWITCH) {
            if (!e->body || !block_terminates(e->body)) return 0;
            for (int i=0;i<e->cases->count;i++)
                if (!block_terminates(((Node*)e->cases->items[i])->body)) return 0;
            return 1;
        }
        return 0;
    }
    default: return 0;
    }
}

static char *block_returning(List *stmts, Ctx *ctx) {
    StrBuf sb; sb_init(&sb);
    gen_stmts_with_defers(&sb, stmts, ctx, "", 1);
    return sb_take(&sb);
}

/* ------------------------------------------------------------------ */
/* gen_import                                                          */
/* ------------------------------------------------------------------ */
static int is_file_specifier(const char *path) {
    return path && path[0] && (path[0]=='.' || path[0]=='/');
}
static char *gen_import(Node *n) {
    StrBuf sb; sb_init(&sb);
    const char *path = n->name ? n->name : "";
    size_t plen = strlen(path);
    StrBuf mod; sb_init(&mod);
    if (is_file_specifier(path) && plen >= 4 && !strcmp(path+plen-4, ".sws")) {
        sb_putn(&mod, path, plen-4); sb_put(&mod, ".h");
    } else {
        sb_put(&mod, path);
    }
    sb_put(&sb, "#include ");
    emit_c_string(&sb, mod.buf);
    sb_put(&sb, "\n/* (symbol imports are resolved by the linker) */");
    sb_free(&mod);
    return sb_take(&sb);
}

/* ------------------------------------------------------------------ */
/* gen_enum                                                            */
/* ------------------------------------------------------------------ */
static char *gen_enum(Node *n, Ctx *ctx) {
    (void)ctx;
    StrBuf sb; sb_init(&sb);
    sb_printf(&sb, "/* enum %s */\n", n->name);
    for (int i=0;i<n->cases->count;i++) {
        Node *c = n->cases->items[i];
        if (c->params->count == 0) {
            sb_printf(&sb, "static SwsVal case_%s_%s(void) { return sws_obj_case(",
                      n->name, c->name);
            emit_c_string(&sb, c->name);
            sb_put(&sb, ", 0, NULL); }\n");
        } else {
            sb_printf(&sb, "static SwsVal case_%s_%s(", n->name, c->name);
            for (int j=0;j<c->params->count;j++) {
                if (j) sb_put(&sb, ", ");
                sb_printf(&sb, "SwsVal _%d", j);
            }
            sb_put(&sb, ") { return sws_obj_case(");
            emit_c_string(&sb, c->name);
            sb_printf(&sb, ", %d, (SwsVal[]){", c->params->count);
            for (int j=0;j<c->params->count;j++) {
                if (j) sb_put(&sb, ", ");
                sb_printf(&sb, "_%d", j);
            }
            sb_put(&sb, "}); }\n");
        }
    }
    return sb_take(&sb);
}

/* ------------------------------------------------------------------ */
/* gen_switch                                                          */
/* ------------------------------------------------------------------ */
static char *gen_switch(Node *n, Ctx *ctx) {
    int my = ++_sw_counter;
    char label[32]; snprintf(label, sizeof(label), "_sw%d", my);
    char subj[32];  snprintf(subj,  sizeof(subj),  "_s%d", my);

    Ctx sw = ctx_copy(ctx);
    ss_add(&sw.break_stack, "switch");
    sw.switch_label = xstrdup(label);

    StrBuf sb; sb_init(&sb);
    sb_printf(&sb, "%s: {\n", label);
    char *sub = gen_expr(n->a, ctx);
    sb_printf(&sb, "  SwsVal %s = %s;\n", subj, sub); free(sub);

    for (int i=0;i<n->cases->count;i++) {
        Node *c = n->cases->items[i];
        StrBuf conds; sb_init(&conds);
        StrBuf binds; sb_init(&binds);
        SS bound = {0};
        for (int j=0;j<c->patterns->count;j++) {
            BindingList bl = {0};
            char *cond = gen_pattern(c->patterns->items[j], subj, &sw, &bl);
            if (j) sb_put(&conds, " || ");
            sb_put(&conds, cond); free(cond);
            for (int k=0;k<bl.count;k++) {
                const char *ns = bl.items[k];
                if (!strncmp(ns,"SwsVal ",7)) ns += 7;
                const char *ne = ns;
                while (*ne && *ne!=' ' && *ne!='=') ne++;
                char *nb = xstrndup(ns, (size_t)(ne-ns));
                if (!ss_has(&bound, nb)) {
                    sb_put(&binds, bl.items[k]); sb_putc(&binds, ' ');
                    ss_add(&bound, nb); ss_add(&sw.locals, nb);
                }
                free(nb);
            }
            bl_clear(&bl);
        }
        char *cs = sb_take(&conds);
        char *bs = sb_take(&binds);

        sb_printf(&sb, "  if (%s) {\n", cs);
        if (bs[0]) sb_printf(&sb, "    %s\n", bs);
        for (int j=0;j<c->body->count;j++) {
            char *s = gen_stmt(c->body->items[j], &sw);
            sb_printf(&sb, "    %s\n", s); free(s);
        }
        sb_printf(&sb, "    goto %s_done;\n  }\n", label);
        free(cs); free(bs);
        ss_free(&bound);
    }

    if (n->body) {
        sb_put(&sb, "  {\n");
        for (int j=0;j<n->body->count;j++) {
            char *s = gen_stmt(n->body->items[j], &sw);
            sb_printf(&sb, "    %s\n", s); free(s);
        }
        sb_put(&sb, "  }\n");
    }
    sb_printf(&sb, "}\n%s_done: ;", label);
    free(sw.switch_label);
    ctx_destroy(&sw);
    return sb_take(&sb);
}

/* ------------------------------------------------------------------ */
/* gen_pattern                                                         */
/* ------------------------------------------------------------------ */
static char *gen_pattern(Node *p, const char *subj, Ctx *ctx, BindingList *bl) {
    StrBuf sb; sb_init(&sb);
    if (!p) { sb_put(&sb, "1"); return sb_take(&sb); }
    switch (p->kind) {
    case N_PAT_ENUM:
        sb_printf(&sb, "sws_case_is(%s, ", subj);
        emit_c_string(&sb, p->name);
        sb_put(&sb, ")");
        for (int i=0;i<p->bindings->count;i++) {
            Node *b = p->bindings->items[i];
            if (b && b->name) {
                StrBuf bs; sb_init(&bs);
                sb_printf(&bs, "SwsVal %s = sws_obj_get(%s, \"_%d\");", b->name, subj, i);
                bl_push(bl, sb_take(&bs));
            }
        }
        break;
    case N_PAT_BIND:
        if (p->name) {
            StrBuf bs; sb_init(&bs);
            sb_printf(&bs, "SwsVal %s = %s;", p->name, subj);
            bl_push(bl, sb_take(&bs));
        }
        sb_put(&sb, "1");
        break;
    case N_PAT_WILD: sb_put(&sb, "1"); break;
    case N_PAT_LIT:
        if (p->isnull)     sb_printf(&sb, "sws_is_null(%s)", subj);
        else if (p->sval) { sb_printf(&sb, "sws_str_eq(%s, ", subj);
                            emit_c_string(&sb, p->sval); sb_put(&sb, ")"); }
        else if (p->is_bool) sb_printf(&sb, "sws_truthy(%s) == %d", subj, p->boolean);
        else               sb_printf(&sb, "sws_num_eq(%s, %.17g)", subj, p->num);
        break;
    case N_PAT_RANGE: {
        char *lo = gen_expr(p->a, ctx), *hi = gen_expr(p->b, ctx);
        sb_printf(&sb, "(sws_ge(%s,%s) && sws_%s(%s,%s))", subj, lo,
                  p->inclusive ? "le" : "lt", subj, hi);
        free(lo); free(hi);
        break;
    }
    case N_PAT_EXPR: {
        char *e = gen_expr(p->a, ctx);
        sb_printf(&sb, "sws_eq(%s, %s)", subj, e); free(e);
        break;
    }
    default: sb_put(&sb, "1");
    }
    return sb_take(&sb);
}

/* ------------------------------------------------------------------ */
/* gen_struct                                                          */
/* ------------------------------------------------------------------ */
static char *gen_struct(Node *n, Ctx *parent) {
    SS members = {0};
    for (int i=0;i<n->fields->count;i++)  ss_add(&members, ((Node*)n->fields->items[i])->name);
    for (int i=0;i<n->methods->count;i++) ss_add(&members, ((Node*)n->methods->items[i])->name);

    StrBuf sb; sb_init(&sb);

    sb_printf(&sb, "static SwsVal ctor_%s(", n->name);
    for (int i=0;i<n->fields->count;i++) {
        if (i) sb_put(&sb, ", ");
        sb_printf(&sb, "SwsVal %s", ((Node*)n->fields->items[i])->name);
    }
    sb_put(&sb, ") {\n  SwsVal _o = sws_obj_new();\n");
    for (int i=0;i<n->fields->count;i++) {
        Node *f = n->fields->items[i];
        sb_printf(&sb, "  sws_obj_set(_o, ");
        emit_c_string(&sb, f->name);
        sb_printf(&sb, ", %s);\n", f->name);
    }
    for (int i=0;i<n->methods->count;i++) {
        Node *m = n->methods->items[i];
        if (m->is_static) continue;
        sb_printf(&sb, "  sws_obj_set(_o, ");
        emit_c_string(&sb, m->name);
        sb_printf(&sb, ", sws_method(_o, _m_%s_%s));\n", n->name, m->name);
    }
    sb_put(&sb, "  return _o;\n}\n");

    for (int i=0;i<n->methods->count;i++) {
        Node *m = n->methods->items[i];
        if (m->is_static) {
            sb_printf(&sb, "static SwsVal _sm_%s_%s(int _argc, SwsVal *_argv) {\n",
                      n->name, m->name);
        } else {
            sb_printf(&sb, "static SwsVal _m_%s_%s(SwsVal self, int _argc, SwsVal *_argv) {\n",
                      n->name, m->name);
            sb_put(&sb, "  (void)_argc;\n");
        }
        for (int j=0;j<m->params->count;j++)
            sb_printf(&sb, "  SwsVal %s = _argv[%d];\n",
                      ((Node*)m->params->items[j])->name, j);
        Ctx mctx = ctx_copy(parent);
        ss_free(&mctx.fields); mctx.fields = members;
        mctx.self_prefix = m->is_static ? NULL : "self";
        ss_free(&mctx.locals); mctx.locals = (SS){0};
        for (int j=0;j<m->params->count;j++)
            ss_add(&mctx.locals, ((Node*)m->params->items[j])->name);
        gen_stmts_with_defers(&sb, m->body, &mctx, "  ", 1);
        sb_put(&sb, "}\n");
        ctx_destroy_no_fields(&mctx);
    }
    ss_free(&members);
    return sb_take(&sb);
}

/* ------------------------------------------------------------------ */
/* gen_class                                                           */
/* ------------------------------------------------------------------ */
static char *gen_class(Node *n, Ctx *parent) {
    SS members = {0};
    for (int i=0;i<n->fields->count;i++)  ss_add(&members, ((Node*)n->fields->items[i])->name);
    for (int i=0;i<n->methods->count;i++) ss_add(&members, ((Node*)n->methods->items[i])->name);

    StrBuf sb; sb_init(&sb);

    if (n->sval) {
        sb_printf(&sb, "/* WARNING: class %s extends %s — inheritance is not "
                        "emulated by this backend */\n", n->name, n->sval);
    }

    sb_printf(&sb, "static SwsVal ctor_%s(", n->name);
    Node *init_m = NULL;
    for (int i=0;i<n->methods->count;i++)
        if (!strcmp(((Node*)n->methods->items[i])->name, "init")) init_m = n->methods->items[i];
    if (init_m) {
        for (int j=0;j<init_m->params->count;j++) {
            if (j) sb_put(&sb, ", ");
            sb_printf(&sb, "SwsVal %s", ((Node*)init_m->params->items[j])->name);
        }
    }
    sb_put(&sb, ") {\n  SwsVal self = sws_obj_new();\n");

    for (int i=0;i<n->fields->count;i++) {
        Node *f = n->fields->items[i];
        if (f->a) {
            Ctx fctx = ctx_copy(parent);
            ss_free(&fctx.fields); fctx.fields = members;
            fctx.self_prefix = "self";
            char *v = gen_expr(f->a, &fctx);
            sb_printf(&sb, "  sws_obj_set(self, ");
            emit_c_string(&sb, f->name);
            sb_printf(&sb, ", %s);\n", v);
            free(v);
            ctx_destroy_no_fields(&fctx);
        }
    }

    if (init_m) {
        Ctx mctx = ctx_copy(parent);
        ss_free(&mctx.fields); mctx.fields = members;
        mctx.self_prefix = "self";
        ss_free(&mctx.locals); mctx.locals = (SS){0};
        for (int j=0;j<init_m->params->count;j++)
            ss_add(&mctx.locals, ((Node*)init_m->params->items[j])->name);
        gen_stmts_with_defers(&sb, init_m->body, &mctx, "  ", 0);
        ctx_destroy_no_fields(&mctx);
    }
    for (int i=0;i<n->methods->count;i++) {
        Node *m = n->methods->items[i];
        if (m->is_static || !strcmp(m->name, "init")) continue;
        sb_printf(&sb, "  sws_obj_set(self, ");
        emit_c_string(&sb, m->name);
        sb_printf(&sb, ", sws_method(self, _m_%s_%s));\n", n->name, m->name);
    }
    sb_put(&sb, "  return self;\n}\n");

    for (int i=0;i<n->methods->count;i++) {
        Node *m = n->methods->items[i];
        if (!strcmp(m->name, "init")) continue;
        if (m->is_static) {
            sb_printf(&sb, "static SwsVal _sm_%s_%s(int _argc, SwsVal *_argv) {\n",
                      n->name, m->name);
        } else {
            sb_printf(&sb, "static SwsVal _m_%s_%s(SwsVal self, int _argc, SwsVal *_argv) {\n",
                      n->name, m->name);
            sb_put(&sb, "  (void)_argc;\n");
        }
        for (int j=0;j<m->params->count;j++)
            sb_printf(&sb, "  SwsVal %s = _argv[%d];\n",
                      ((Node*)m->params->items[j])->name, j);
        Ctx mctx = ctx_copy(parent);
        ss_free(&mctx.fields); mctx.fields = members;
        mctx.self_prefix = m->is_static ? NULL : "self";
        ss_free(&mctx.locals); mctx.locals = (SS){0};
        for (int j=0;j<m->params->count;j++)
            ss_add(&mctx.locals, ((Node*)m->params->items[j])->name);
        gen_stmts_with_defers(&sb, m->body, &mctx, "  ", 1);
        sb_put(&sb, "}\n");
        ctx_destroy_no_fields(&mctx);
    }
    ss_free(&members);
    return sb_take(&sb);
}

/* ------------------------------------------------------------------ */
/* Builtins                                                            */
/* ------------------------------------------------------------------ */
typedef struct { const char *src, *tgt; int variadic; } Builtin;
static const Builtin BUILTINS[] = {
    {"print",       "sws_println", 1},
    {"String",      "sws_to_string", 0},
    {"Int",         "sws_to_int", 0},
    {"Double",      "sws_to_double", 0},
    {"Bool",        "sws_to_bool", 0},
    {"abs",         "sws_abs", 0},
    {"min",         "sws_min", 0},
    {"max",         "sws_max", 0},
    {"round",       "sws_round", 0},
    {"floor",       "sws_floor", 0},
    {"ceil",        "sws_ceil", 0},
    {"_force",      "sws_force", 0},
    {"_range",      "sws_range", 0},
    {"_rangeIn",    "sws_range_in", 0},
    {NULL, NULL, 0}
};
static const char *builtin_target(const char *name) {
    for (int i=0;BUILTINS[i].src;i++) if (!strcmp(BUILTINS[i].src,name)) return BUILTINS[i].tgt;
    return NULL;
}
static int builtin_variadic(const char *name) {
    for (int i=0;BUILTINS[i].src;i++)
        if (!strcmp(BUILTINS[i].src,name)) return BUILTINS[i].variadic;
    return 0;
}

/* ------------------------------------------------------------------ */
/* gen_expr                                                            */
/* ------------------------------------------------------------------ */
static char *gen_expr(Node *n, Ctx *ctx) {
    StrBuf sb; sb_init(&sb);
    switch (n->kind) {

    case N_IF_LET: case N_GUARD_LET:
        return xstrdup("sws_undef()");

    case N_LIT:
        if (n->isnull)       return xstrdup("sws_null()");
        if (n->sval)         { sb_put(&sb, "sws_str("); emit_c_string(&sb, n->sval); sb_put(&sb, ")"); return sb_take(&sb); }
        if (n->is_bool)      return xstrdup(n->boolean ? "sws_bool(1)" : "sws_bool(0)");
        sb_printf(&sb, "sws_num(%.17g)", n->num); return sb_take(&sb);

    case N_ID: {
        if (!n->name) { codegen_error(n->line,n->col,"N_ID with NULL name"); return xstrdup("sws_undef()"); }
        if (!strcmp(n->name, "self") && ctx->self_prefix) return xstrdup(ctx->self_prefix);
        if (!strcmp(n->name, "super")) return xstrdup("sws_undef() /* super not supported */");
        if (ctx->fields.count && ss_has(&ctx->fields, n->name)
            && !ss_has(&ctx->locals, n->name)) {
            const char *p = ctx->self_prefix ? ctx->self_prefix : "self";
            sb_printf(&sb, "sws_obj_get(%s, ", p);
            emit_c_string(&sb, n->name);
            sb_put(&sb, ")");
            return sb_take(&sb);
        }
        if (!builtin_target(n->name) && !ss_has(&ctx->globals, n->name)
            && !ss_has(&ctx->locals, n->name))
            codegen_error(n->line, n->col, "Undefined identifier '%s'", n->name);
        emit_name(&sb, ctx, n->name);
        return sb_take(&sb);
    }

    case N_ENUM_CASE: {
        const char *en = enum_find(ctx, n->name);
        if (en) { sb_printf(&sb, "case_%s_%s()", en, n->name); return sb_take(&sb); }
        return xstrdup("sws_undef()");
    }

    case N_TEMPLATE: return gen_template(n, ctx);

    case N_ARRAY: {
        sb_printf(&sb, "sws_arr(%d, (SwsVal[]){", n->items->count);
        for (int i=0;i<n->items->count;i++) {
            if (i) sb_put(&sb, ", ");
            char *e = gen_expr(n->items->items[i], ctx);
            sb_put(&sb, e); free(e);
        }
        sb_put(&sb, "})");
        return sb_take(&sb);
    }

    case N_DICT: {
        sb_printf(&sb, "sws_obj_from(%d, (const char*[]){", n->pairs->count);
        for (int i=0;i<n->pairs->count;i++) {
            if (i) sb_put(&sb, ", ");
            Node *pr = n->pairs->items[i];
            if (pr->a->kind == N_LIT && pr->a->sval) {
                emit_c_string(&sb, pr->a->sval);
            } else {
                sb_put(&sb, "\"\"");
            }
        }
        sb_put(&sb, "}, (SwsVal[]){");
        for (int i=0;i<n->pairs->count;i++) {
            if (i) sb_put(&sb, ", ");
            char *v = gen_expr(((Node*)n->pairs->items[i])->b, ctx);
            sb_put(&sb, v); free(v);
        }
        sb_put(&sb, "})");
        return sb_take(&sb);
    }

    case N_TUPLE:
        return gen_expr(n, ctx);

    case N_MEMBER: {
        /* 1. Enum member access: Color.red -> case_Color_red()
         * Must be checked BEFORE gen_expr, because `Color` is not a
         * variable in the C backend (only `case_Color_red()` exists). */
        if (n->a->kind == N_ID && n->a->name) {
            for (int i = 0; i < ctx->enum_count; i++) {
                if (strcmp(ctx->enums[i].name, n->a->name) == 0) {
                    sb_printf(&sb, "case_%s_%s()", n->a->name, n->name);
                    return sb_take(&sb);
                }
            }
        }

        /* 2. Regular object property access: p.x -> sws_obj_get(p, "x") */
        char *o = gen_expr(n->a, ctx);
        if (n->optional) sb_printf(&sb, "sws_opt_get(%s, ", o);
        else             sb_printf(&sb, "sws_obj_get(%s, ", o);
        emit_c_string(&sb, n->name ? n->name : "");
        sb_put(&sb, ")");
        free(o);
        return sb_take(&sb);
    }

    case N_INDEX: {
        char *o = gen_expr(n->a, ctx), *i = gen_expr(n->b, ctx);
        sb_printf(&sb, "sws_index(%s, %s)", o, i);
        free(o); free(i); return sb_take(&sb);
    }

    case N_CALL: return gen_call(n, ctx);

    case N_FORCE: { char *a = gen_expr(n->a, ctx);
                    sb_printf(&sb, "sws_force(%s)", a); free(a); return sb_take(&sb); }

    case N_UNARY: {
        char *a = gen_expr(n->a, ctx);
        if (!strcmp(n->op, "!"))       sb_printf(&sb, "sws_bool(!sws_truthy(%s))", a);
        else if (!strcmp(n->op, "-"))  sb_printf(&sb, "sws_neg(%s)", a);
        else if (!strcmp(n->op, "try?")) sb_printf(&sb, "sws_try_opt(%s)", a);
        else if (!strcmp(n->op, "try!")) sb_printf(&sb, "sws_try_force(%s)", a);
        else if (!strcmp(n->op, "try"))  sb_printf(&sb, "(%s)", a);
        else sb_printf(&sb, "(%s%s)", n->op?n->op:"", a);
        free(a); return sb_take(&sb);
    }

    case N_BIN: {
        char *L = gen_expr(n->a, ctx), *R = gen_expr(n->b, ctx);
        const char *op = n->op ? n->op : "+";
        if      (!strcmp(op, "+"))   sb_printf(&sb, "sws_add(%s, %s)", L, R);
        else if (!strcmp(op, "-"))   sb_printf(&sb, "sws_sub(%s, %s)", L, R);
        else if (!strcmp(op, "*"))   sb_printf(&sb, "sws_mul(%s, %s)", L, R);
        else if (!strcmp(op, "/"))   sb_printf(&sb, "sws_div(%s, %s)", L, R);
        else if (!strcmp(op, "%"))   sb_printf(&sb, "sws_mod(%s, %s)", L, R);
        else if (!strcmp(op, "=="))  sb_printf(&sb, "sws_eq(%s, %s)", L, R);
        else if (!strcmp(op, "!="))  sb_printf(&sb, "sws_bool(!sws_truthy(sws_eq(%s,%s)))", L, R);
        else if (!strcmp(op, "<"))   sb_printf(&sb, "sws_lt(%s, %s)", L, R);
        else if (!strcmp(op, ">"))   sb_printf(&sb, "sws_gt(%s, %s)", L, R);
        else if (!strcmp(op, "<="))  sb_printf(&sb, "sws_le(%s, %s)", L, R);
        else if (!strcmp(op, ">="))  sb_printf(&sb, "sws_ge(%s, %s)", L, R);
        else if (!strcmp(op, "&&"))  sb_printf(&sb, "(sws_truthy(%s) ? (%s) : sws_bool(0))", L, R);
        else if (!strcmp(op, "||"))  sb_printf(&sb, "(sws_truthy(%s) ? sws_bool(1) : (%s))", L, R);
        else if (!strcmp(op, "??"))  sb_printf(&sb, "(sws_is_nullish(%s) ? (%s) : (%s))", L, R, L);
        else if (!strcmp(op, "..<")) sb_printf(&sb, "sws_range(%s, %s)", L, R);
        else if (!strcmp(op, "...")) sb_printf(&sb, "sws_range_in(%s, %s)", L, R);
        else sb_printf(&sb, "/* unsupported op %s */sws_undef()", op);
        free(L); free(R); return sb_take(&sb);
    }

    case N_TERNARY: {
        char *c = gen_expr(n->a, ctx), *t = gen_expr(n->b, ctx), *e = gen_expr(n->c, ctx);
        sb_printf(&sb, "(sws_truthy(%s) ? %s : %s)", c, t, e);
        free(c); free(t); free(e); return sb_take(&sb);
    }

    case N_ASSIGN: {
        char *L = gen_expr(n->a, ctx), *R = gen_expr(n->b, ctx);
        const char *op = n->op ? n->op : "=";
        if (!strcmp(op, "="))       sb_printf(&sb, "(%s = %s)", L, R);
        else if (!strcmp(op, "+=")) sb_printf(&sb, "(%s = sws_add(%s, %s))", L, L, R);
        else if (!strcmp(op, "-=")) sb_printf(&sb, "(%s = sws_sub(%s, %s))", L, L, R);
        else if (!strcmp(op, "*=")) sb_printf(&sb, "(%s = sws_mul(%s, %s))", L, L, R);
        else if (!strcmp(op, "/=")) sb_printf(&sb, "(%s = sws_div(%s, %s))", L, L, R);
        else                        sb_printf(&sb, "(%s = sws_undef())", L);
        free(L); free(R); return sb_take(&sb);
    }

    case N_IF: return gen_if_expr(n, ctx);

    case N_SWITCH: {
        char *s = gen_switch(n, ctx);
        sb_printf(&sb, "({ %s sws_undef(); })", s);
        free(s); return sb_take(&sb);
    }

    case N_CLOSURE: return gen_closure(n, ctx);

    case N_AWAIT: {
        char *a = gen_expr(n->a, ctx);
        sb_printf(&sb, "/* await */ %s", a); free(a); return sb_take(&sb);
    }

    default:
        codegen_error(n->line, n->col, "codegen: unknown expr kind %d", n->kind);
        return xstrdup("sws_undef()");
    }
}

/* ------------------------------------------------------------------ */
/* gen_call                                                            */
/* ------------------------------------------------------------------ */
static char *gen_call(Node *n, Ctx *ctx) {
    if (!n->a) { codegen_error(n->line,n->col,"call with NULL callee"); return xstrdup("sws_undef()"); }

    if (n->a->kind == N_MEMBER && n->a->a && n->a->a->kind == N_ID &&
        n->a->a->name && !strcmp(n->a->a->name,"super") &&
        n->a->name && !strcmp(n->a->name,"init")) {
        StrBuf sb; sb_init(&sb);
        sb_put(&sb, "/* super.init not supported by C backend */ sws_undef()");
        return sb_take(&sb);
    }

    if (n->a->kind == N_ENUM_CASE) {
        const char *en = enum_find(ctx, n->a->name);
        if (en) {
            StrBuf sb; sb_init(&sb);
            sb_printf(&sb, "case_%s_%s(", en, n->a->name);
            for (int i=0;i<n->args->count;i++) {
                if (i) sb_put(&sb, ", ");
                char *v = gen_expr(((Node*)n->args->items[i])->a, ctx);
                sb_put(&sb, v); free(v);
            }
            sb_put(&sb, ")");
            return sb_take(&sb);
        }
    }

    if (n->a->kind == N_ID && n->a->name &&
        (ss_has(&ctx->struct_names, n->a->name) || ss_has(&ctx->class_names, n->a->name))) {
        StrBuf sb; sb_init(&sb);
        sb_printf(&sb, "ctor_%s(", n->a->name);
        for (int i=0;i<n->args->count;i++) {
            if (i) sb_put(&sb, ", ");
            Node *a = n->args->items[i];
            char *v = gen_expr(a->a, ctx);
            sb_put(&sb, v); free(v);
        }
        sb_put(&sb, ")");
        return sb_take(&sb);
    }

    if (n->a->kind == N_MEMBER && n->a->a && n->a->a->kind == N_ID && n->a->a->name &&
        (ss_has(&ctx->struct_names, n->a->a->name) || ss_has(&ctx->class_names, n->a->a->name))) {
        StrBuf sb; sb_init(&sb);
        sb_printf(&sb, "_sm_%s_%s(", n->a->a->name, n->a->name ? n->a->name : "");
        sb_printf(&sb, "%d", n->args->count);
        for (int i=0;i<n->args->count;i++) {
            sb_put(&sb, ", ");
            char *v = gen_expr(((Node*)n->args->items[i])->a, ctx);
            sb_put(&sb, v); free(v);
        }
        sb_put(&sb, ")");
        return sb_take(&sb);
    }

    if (n->a->kind == N_ID && n->a->name && builtin_target(n->a->name)) {
        StrBuf sb; sb_init(&sb);
        sb_printf(&sb, "%s(", builtin_target(n->a->name));
        if (builtin_variadic(n->a->name)) {
            sb_printf(&sb, "%d", n->args->count);
            if (n->args->count) sb_put(&sb, ", (SwsVal[]){");
            for (int i=0;i<n->args->count;i++) {
                if (i) sb_put(&sb, ", ");
                char *v = gen_expr(((Node*)n->args->items[i])->a, ctx);
                sb_put(&sb, v); free(v);
            }
            if (n->args->count) sb_put(&sb, "}");
        } else {
            for (int i=0;i<n->args->count;i++) {
                if (i) sb_put(&sb, ", ");
                char *v = gen_expr(((Node*)n->args->items[i])->a, ctx);
                sb_put(&sb, v); free(v);
            }
        }
        sb_put(&sb, ")");
        return sb_take(&sb);
    }

    /* direct function call */
    if (n->a->kind == N_ID && n->a->name && ss_has(&ctx->func_names, n->a->name)) {
        StrBuf sb; sb_init(&sb);
        sb_printf(&sb, "fn_%s(", n->a->name);
        for (int i = 0; i < n->args->count; i++) {
            if (i) sb_put(&sb, ", ");
            char *v = gen_expr(((Node*)n->args->items[i])->a, ctx);
            sb_put(&sb, v); free(v);
        }
        sb_put(&sb, ")");
        return sb_take(&sb);
    }

    /* Enum case call with payload: Shape.circle(2.5) */
    if (n->a->kind == N_MEMBER && n->a->a && n->a->a->kind == N_ID && n->a->a->name) {
        for (int i = 0; i < ctx->enum_count; i++) {
            if (strcmp(ctx->enums[i].name, n->a->a->name) == 0) {
                StrBuf sb; sb_init(&sb);
                sb_printf(&sb, "case_%s_%s(", n->a->a->name, n->a->name);
                for (int j = 0; j < n->args->count; j++) {
                    if (j) sb_put(&sb, ", ");
                    char *v = gen_expr(((Node*)n->args->items[j])->a, ctx);
                    sb_put(&sb, v); free(v);
                }
                sb_put(&sb, ")");
                return sb_take(&sb);
            }
        }
    }

    StrBuf sb; sb_init(&sb);
    char *callee = gen_expr(n->a, ctx);
    if (n->args->count) {
        sb_printf(&sb, "sws_call(%s, %d, (SwsVal[]){", callee, n->args->count);
        for (int i=0;i<n->args->count;i++) {
            if (i) sb_put(&sb, ", ");
            char *v = gen_expr(((Node*)n->args->items[i])->a, ctx);
            sb_put(&sb, v); free(v);
        }
        sb_put(&sb, "})");
    } else {
        sb_printf(&sb, "sws_call(%s, 0, NULL)", callee);
    }
    free(callee);
    return sb_take(&sb);
}

/* ------------------------------------------------------------------ */
/* gen_if_expr                                                         */
/* ------------------------------------------------------------------ */
static char *gen_if_expr(Node *n, Ctx *ctx) {
    StrBuf sb; sb_init(&sb);
    char *cond = gen_expr(n->a, ctx);
    char *then_code = block_returning(n->b->stmts, ctx);
    sb_printf(&sb, "({ SwsVal _r; if (sws_truthy(%s)) { %s } ",
              cond, then_code);
    free(cond); free(then_code);
    if (!n->c) sb_put(&sb, "else _r = sws_null(); ");
    else {
        char *e = block_returning(n->c->stmts, ctx);
        sb_printf(&sb, "else { %s } ", e); free(e);
    }
    sb_put(&sb, "_r; })");
    return sb_take(&sb);
}

/* ------------------------------------------------------------------ */
/* gen_closure                                                         */
/* ------------------------------------------------------------------ */
static void collect_dollar(Node *n, int *max, int *found) {
    if (!n) return;
    if (n->kind == N_CLOSURE) return;
    if (n->kind == N_ID && n->name && n->name[0]=='$') {
        int idx = atoi(n->name+1);
        if (idx > *max) *max = idx;
        *found = 1;
    }
    collect_dollar(n->a,max,found); collect_dollar(n->b,max,found); collect_dollar(n->c,max,found);
    #define W(l) do { if (n->l) for (int i=0;i<n->l->count;i++) collect_dollar((Node*)n->l->items[i],max,found); } while(0)
    W(stmts); W(params); W(fields); W(methods); W(cases); W(body);
    W(args); W(items); W(pairs); W(patterns); W(bindings); W(parts);
    #undef W
}

static char *gen_closure(Node *n, Ctx *ctx) {
    (void)ctx;
    StrBuf sb; sb_init(&sb);
    StrBuf params; sb_init(&params);
    if (n->params && n->params->count) {
        for (int i=0;i<n->params->count;i++) {
            if (i) sb_put(&params, ", ");
            sb_put(&params, ((Node*)n->params->items[i])->name);
        }
    } else {
        int max = -1, found = 0;
        if (n->body) for (int i=0;i<n->body->count;i++)
            collect_dollar((Node*)n->body->items[i], &max, &found);
        if (found) for (int i=0;i<=max;i++) {
            if (i) sb_put(&params, ", ");
            sb_printf(&params, "$%d", i);
        }
    }
    sb_printf(&sb, "/* closure (no capture) */ sws_lambda((void*)0)");
    sb_free(&params);
    return sb_take(&sb);
}

/* ------------------------------------------------------------------ */
/* gen_template                                                        */
/* ------------------------------------------------------------------ */
static char *gen_template(Node *n, Ctx *ctx) {
    StrBuf sb; sb_init(&sb);
    sb_printf(&sb, "sws_concat(%d, (SwsVal[]){", n->parts->count);
    for (int i=0;i<n->parts->count;i++) {
        if (i) sb_put(&sb, ", ");
        Node *p = n->parts->items[i];
        if (p->sval && !strcmp(p->sval, "text")) {
            sb_put(&sb, "sws_str(");
            emit_c_string(&sb, p->name ? p->name : "");
            sb_put(&sb, ")");
        } else {
            char *e = gen_expr(p->a, ctx);
            sb_put(&sb, e); free(e);
        }
    }
    sb_put(&sb, "})");
    return sb_take(&sb);
}

/* ------------------------------------------------------------------ */
/* gen_program: assemble the whole C file                              */
/* ------------------------------------------------------------------ */
static void emit_fwd_decl(StrBuf *sb, Node *s, Ctx *ctx) {
    (void)ctx;
    if (s->kind == N_FUNC) {
        sb_printf(sb, "static SwsVal fn_%s(", s->name);
        for (int j=0;j<s->params->count;j++) {
            if (j) sb_put(sb, ", ");
            sb_printf(sb, "SwsVal %s", ((Node*)s->params->items[j])->name);
        }
        sb_put(sb, ");\n");
    } else if (s->kind == N_STRUCT || s->kind == N_CLASS) {
        sb_printf(sb, "static SwsVal ctor_%s(", s->name);
        int n_first = 1;
        if (s->kind == N_STRUCT) {
            for (int j=0;j<s->fields->count;j++) {
                Node *f = s->fields->items[j];
                if (f->is_computed) continue;
                if (!n_first) sb_put(sb, ", ");
                n_first = 0;
                sb_printf(sb, "SwsVal %s", f->name);
            }
        } else {
            for (int j=0;j<s->methods->count;j++) {
                Node *m = s->methods->items[j];
                if (strcmp(m->name, "init")) continue;
                for (int k=0;k<m->params->count;k++) {
                    if (!n_first) sb_put(sb, ", ");
                    n_first = 0;
                    sb_printf(sb, "SwsVal %s", ((Node*)m->params->items[k])->name);
                }
            }
        }
        sb_put(sb, ");\n");
        for (int j=0;j<s->methods->count;j++) {
            Node *m = s->methods->items[j];
            if (!m->is_static) continue;
            sb_printf(sb, "static SwsVal _sm_%s_%s(int _argc, SwsVal *_argv);\n",
                      s->name, m->name);
        }
        for (int j=0;j<s->methods->count;j++) {
            Node *m = s->methods->items[j];
            if (m->is_static || !strcmp(m->name,"init")) continue;
            sb_printf(sb, "static SwsVal _m_%s_%s(SwsVal self, int _argc, SwsVal *_argv);\n",
                      s->name, m->name);
        }
    } else if (s->kind == N_ENUM) {
        for (int j=0;j<s->cases->count;j++) {
            Node *c = s->cases->items[j];
            if (c->params->count == 0)
                sb_printf(sb, "static SwsVal case_%s_%s(void);\n", s->name, c->name);
            else {
                sb_printf(sb, "static SwsVal case_%s_%s(", s->name, c->name);
                for (int k=0;k<c->params->count;k++) {
                    if (k) sb_put(sb, ", ");
                    sb_printf(sb, "SwsVal _%d", k);
                }
                sb_put(sb, ");\n");
            }
        }
    }
}

char *gen_c_program(Node *prog) {
    _sw_counter = _let_bind_counter = 0;

    Ctx ctx; memset(&ctx, 0, sizeof(ctx));
    collect_decls(prog, &ctx);

    StrBuf fwd, gdecl, defs, initb;
    sb_init(&fwd); sb_init(&gdecl); sb_init(&defs); sb_init(&initb);

    for (int i=0;i<prog->stmts->count;i++)
        emit_fwd_decl(&fwd, prog->stmts->items[i], &ctx);

    for (int i=0;i<ctx.globals.count;i++)
        sb_printf(&gdecl, "static SwsVal g_%s;\n", ctx.globals.items[i]);

    for (int i=0;i<prog->stmts->count;i++) {
        Node *s = prog->stmts->items[i];
        if (s->kind == N_IMPORT) {
            char *c = gen_import(s);
            sb_printf(&defs, "%s\n", c); free(c);
            continue;
        }
        if (s->kind == N_FUNC || s->kind == N_STRUCT ||
            s->kind == N_CLASS || s->kind == N_ENUM) {
            char *c = gen_stmt(s, &ctx);
            sb_printf(&defs, "%s\n", c); free(c);
        } else {
            ctx.top_level = 1;
            char *c = gen_stmt(s, &ctx);
            ctx.top_level = 0;
            sb_printf(&initb, "#line %d \"source.sws\"\n  %s\n", s->line, c);
            free(c);
        }
    }

    StrBuf out; sb_init(&out);
    sb_put(&out, "/* Generated by swsc → C backend */\n");
    sb_put(&out, "#include \"sws_rt.h\"\n#include <setjmp.h>\n#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n\n");
    sb_put(&out, sb_take(&fwd));
    sb_put(&out, "\n");
    sb_put(&out, sb_take(&gdecl));
    sb_put(&out, "\n");
    sb_put(&out, sb_take(&defs));
    sb_put(&out, "\nvoid _sws_module_init(void) {\n");
    sb_put(&out, sb_take(&initb));
    sb_put(&out, "}\n\n#ifndef SWS_NO_MAIN\nint main(void) { _sws_module_init(); return 0; }\n#endif\n");

    ss_free(&ctx.globals); ss_free(&ctx.struct_names); ss_free(&ctx.class_names);
    ss_free(&ctx.func_names);
    ss_free(&ctx.locals);  ss_free(&ctx.break_stack);  ss_free(&ctx.fields);
    for (int i=0;i<ctx.enum_case_count;i++) { free(ctx.enum_cases[i].k); free(ctx.enum_cases[i].v); }
    free(ctx.enum_cases);
    for (int i=0;i<ctx.enum_count;i++) { free(ctx.enums[i].name); ss_free(&ctx.enums[i].cases); }
    free(ctx.enums);

    if (_err) { sb_free(&out); return NULL; }
    return sb_take(&out);
}

char *gen_c_get_sourcemap(const char *source_file) {
    (void)source_file;
    return xstrdup("{}");
}