#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <setjmp.h>

#define MAX_JB 64

typedef struct {
    TokenList *toks;
    int pos;
    int err_line, err_col;
    char err_msg[512];
    int has_err;
    jmp_buf jbs[MAX_JB];
    int jb_top;
} P;

static void p_err(P *p, int line, int col, const char *fmt, ...) {
    p->err_line = line; p->err_col = col;
    va_list ap; va_start(ap, fmt);
    vsnprintf(p->err_msg, sizeof(p->err_msg), fmt, ap);
    va_end(ap);
    p->has_err = 1;
    longjmp(p->jbs[p->jb_top - 1], 1);
}

static Token *peek(P *p, int n) {
    int i = p->pos + n;
    if (i < 0) i = 0;
    if (i >= p->toks->count) i = p->toks->count - 1;
    return &p->toks->items[i];
}
static Token *next_tok(P *p) { return &p->toks->items[p->pos++]; }

static int tok_is(Token *t, const char *type, const char *val) {
    if (type[0] == 'o' && t->type != T_OP) return 0;
    if (type[0] == 'k' && t->type != T_KW) return 0;
    if (type[0] == 'i' && t->type != T_ID) return 0;
    if (type[0] == 's' && t->type != T_STR) return 0;
    if (type[0] == 'n' && t->type != T_NUM) return 0;
    if (type[0] == 'e' && t->type != T_EOF) return 0;
    if (val && (!t->value || strcmp(t->value, val) != 0)) return 0;
    return 1;
}

static int check_op(P *p, const char *v) { return tok_is(peek(p, 0), "op", v); }
static int check_kw(P *p, const char *v) { return tok_is(peek(p, 0), "kw", v); }
static int check_id_tok(P *p)            { return peek(p, 0)->type == T_ID; }
static int check_eof(P *p)               { return peek(p, 0)->type == T_EOF; }

static int match_op(P *p, const char *v) { if (check_op(p, v)) { p->pos++; return 1; } return 0; }
static int match_kw(P *p, const char *v) { if (check_kw(p, v)) { p->pos++; return 1; } return 0; }

static Token *expect_op(P *p, const char *v) {
    if (!check_op(p, v)) {
        Token *g = peek(p, 0);
        p_err(p, g->line, g->col, "Ожидалось op '%s', получено '%s'",
              v, g->value ? g->value : "<eof>");
    }
    return next_tok(p);
}
static Token *expect_kw(P *p, const char *v) {
    if (!check_kw(p, v)) {
        Token *g = peek(p, 0);
        p_err(p, g->line, g->col, "Ожидалось kw '%s', получено '%s'",
              v, g->value ? g->value : "<eof>");
    }
    return next_tok(p);
}
static Token *expect_id(P *p) {
    if (!check_id_tok(p)) {
        Token *g = peek(p, 0);
        p_err(p, g->line, g->col, "Ожидалось имя, получено '%s'",
              g->value ? g->value : "<eof>");
    }
    return next_tok(p);
}
static Token *expect_member_name(P *p) {
    Token *t = peek(p, 0);
    if (t->type == T_ID) return next_tok(p);
    if (t->type == T_KW && t->value && strcmp(t->value, "init") == 0)
        return next_tok(p);
    p_err(p, t->line, t->col, "Ожидалось имя поля, получено '%s'",
          t->value ? t->value : "<eof>");
    return NULL;
}
static Token *expect_str(P *p) {
    Token *t = peek(p, 0);
    if (t->type != T_STR) {
        p_err(p, t->line, t->col, "Ожидалась строка, получено '%s'",
              t->value ? t->value : "<eof>");
    }
    return next_tok(p);
}

/* forward */
static Node *parse_stmt(P *p);
static Node *parse_expr(P *p, double min_prec, int no_trailing);
static Node *parse_type(P *p);
static Node *parse_closure(P *p);
static Node *parse_switch(P *p);
static Node *parse_if(P *p);
static Node *parse_func(P *p);
static Node *parse_struct(P *p);
static Node *parse_enum(P *p);
static Node *parse_import(P *p);
static Node *parse_export(P *p);
static Node *parse_one_binding(P *p);
static Node *parse_guard(P *p);
static Node *parse_try(P *p);
static Node *parse_throw(P *p);
static Node *parse_async_func(P *p);
static Node *parse_class(P *p);
static Node *parse_init(P *p);

static Node *parse_program(P *p) {
    Node *n = node_new(N_PROGRAM, 1, 1);
    n->stmts = list_new();
    while (!check_eof(p)) list_push(n->stmts, parse_stmt(p));
    return n;
}

static Node *parse_let(P *p) {
    Token *kw = next_tok(p);
    Token *id = expect_id(p);
    Node *n = node_new(N_LET, kw->line, kw->col);
    n->sval = xstrdup(kw->value);  /* "let" | "var" */
    n->name = xstrdup(id->value);
    if (match_op(p, ":")) n->type = parse_type(p);
    if (match_op(p, "=")) n->a = parse_expr(p, 0, 0);
    match_op(p, ";");
    return n;
}

static Node *parse_one_binding(P *p) {
    Token *kw = next_tok(p);           /* let | var */
    Token *id = expect_id(p);
    Node *b = node_new(N_LET, kw->line, kw->col);
    b->sval = xstrdup(kw->value);
    b->name = xstrdup(id->value);
    expect_op(p, "=");
    b->a = parse_expr(p, 0, 1);        /* noTrailing */
    return b;
}

static Node *parse_import(P *p) {
    Token *kw = expect_kw(p, "import");
    Node *n = node_new(N_IMPORT, kw->line, kw->col);
    n->items = list_new();

    if (match_op(p, "*")) {
        expect_kw(p, "as");
        Token *alias = expect_id(p);
        expect_kw(p, "from");
        Token *src = expect_str(p);
        n->op = xstrdup("namespace");
        n->name = xstrdup(src->value);
        Node *spec = node_new(N_PARAM, alias->line, alias->col);
        spec->name = xstrdup(alias->value);
        list_push(n->items, spec);
    } else if (match_op(p, "{")) {
        n->op = xstrdup("named");
        if (!check_op(p, "}")) {
            for (;;) {
                Token *id = expect_id(p);
                const char *alias = NULL;
                if (match_kw(p, "as")) alias = xstrdup(expect_id(p)->value);
                Node *spec = node_new(N_PARAM, id->line, id->col);
                spec->name = xstrdup(id->value);
                spec->sval = alias;
                list_push(n->items, spec);
                if (!match_op(p, ",")) break;
                if (check_op(p, "}")) break;
            }
        }
        expect_op(p, "}");
        expect_kw(p, "from");
        Token *src = expect_str(p);
        n->name = xstrdup(src->value);
    } else {
        Token *id = expect_id(p);
        expect_kw(p, "from");
        Token *src = expect_str(p);
        n->op = xstrdup("default");
        n->name = xstrdup(src->value);
        Node *spec = node_new(N_PARAM, id->line, id->col);
        spec->name = xstrdup(id->value);
        list_push(n->items, spec);
    }

    match_op(p, ";");
    return n;
}

static Node *parse_export(P *p) {
    Token *kw = expect_kw(p, "export");
    Node *decl;
    if      (check_kw(p, "let") || check_kw(p, "var")) decl = parse_let(p);
    else if (check_kw(p, "func"))                       decl = parse_func(p);
    else if (check_kw(p, "struct"))                     decl = parse_struct(p);
    else if (check_kw(p, "enum"))                       decl = parse_enum(p);
    else if (check_kw(p, "class"))                      decl = parse_class(p);
    else if (check_kw(p, "async") &&
             peek(p, 1)->type == T_KW && peek(p, 1)->value &&
             strcmp(peek(p, 1)->value, "func") == 0)
        decl = parse_async_func(p);
    else {
        Token *g = peek(p, 0);
        p_err(p, g->line, g->col,
              "После export ожидалось let, var, func, struct или enum, получено '%s'",
              g->value ? g->value : "<eof>");
    }
    (void)kw;
    decl->exported = 1;
    return decl;
}

static Node *parse_type(P *p) {
    if (match_op(p, "[")) {
        Node *inner = parse_type(p);
        expect_op(p, "]");
        Node *t = node_new(N_TYPE_ARRAY, inner->line, inner->col);
        t->a = inner;
        if (match_op(p, "?")) {
            Node *o = node_new(N_TYPE_OPT, t->line, t->col);
            o->a = t;
            return o;
        }
        return t;
    }
    if (check_op(p, "(")) {
        Token *lp = next_tok(p);
        Node *t = node_new(N_TYPE_TUPLE, lp->line, lp->col);
        t->items = list_new();
        while (!check_op(p, ")")) {
            list_push(t->items, parse_type(p));
            if (!match_op(p, ",")) break;
        }
        expect_op(p, ")");
        if (match_op(p, "?")) {
            Node *o = node_new(N_TYPE_OPT, t->line, t->col);
            o->a = t;
            return o;
        }
        return t;
    }
    Token *id = expect_id(p);
    Node *t = node_new(N_TYPE_NAMED, id->line, id->col);
    t->name = xstrdup(id->value);
    if (match_op(p, "<")) {
        int depth = 1;
        while (depth > 0 && !check_eof(p)) {
            if (check_op(p, "<")) depth++;
            else if (check_op(p, ">")) depth--;
            if (depth > 0) next_tok(p);
        }
        expect_op(p, ">");
    }
    if (match_op(p, "?")) {
        Node *o = node_new(N_TYPE_OPT, t->line, t->col);
        o->a = t;
        return o;
    }
    return t;
}

static Node *parse_func(P *p) {
    Token *kw = expect_kw(p, "func");
    Token *id = expect_id(p);
    Node *n = node_new(N_FUNC, kw->line, kw->col);
    n->name = xstrdup(id->value);
    n->params = list_new();
    expect_op(p, "(");
    if (!check_op(p, ")")) {
        for (;;) {
            const char *label = NULL, *pname = NULL;
            if (check_op(p, "_")) {
                next_tok(p);
                pname = xstrdup(expect_id(p)->value);
            } else if (check_id_tok(p) && peek(p, 1)->type == T_ID) {
                label = xstrdup(next_tok(p)->value);
                pname = xstrdup(expect_id(p)->value);
            } else {
                pname = xstrdup(expect_id(p)->value);
                label = xstrdup(pname);
            }
            Node *pr = node_new(N_PARAM, kw->line, kw->col);
            pr->name = pname;
            pr->sval = label;
            if (match_op(p, ":")) pr->type = parse_type(p);
            list_push(n->params, pr);
            if (!match_op(p, ",")) break;
            if (check_op(p, ")")) break;
        }
    }
    expect_op(p, ")");
    if (match_op(p, "->")) n->retType = parse_type(p);
    expect_op(p, "{");
    n->body = list_new();
    while (!check_op(p, "}")) {
        if (check_eof(p)) {
            Token *g = peek(p, 0);
            p_err(p, g->line, g->col, "Незакрытая функция");
        }
        list_push(n->body, parse_stmt(p));
    }
    expect_op(p, "}");
    return n;
}

static Node *parse_init(P *p) {
    Token *kw = expect_kw(p, "init");
    Node *n = node_new(N_FUNC, kw->line, kw->col);
    n->name = xstrdup("init");
    n->params = list_new();
    expect_op(p, "(");
    if (!check_op(p, ")")) {
        for (;;) {
            const char *label = NULL, *pname = NULL;
            if (check_op(p, "_")) {
                next_tok(p);
                pname = xstrdup(expect_id(p)->value);
            } else if (check_id_tok(p) && peek(p, 1)->type == T_ID) {
                label = xstrdup(next_tok(p)->value);
                pname = xstrdup(expect_id(p)->value);
            } else {
                pname = xstrdup(expect_id(p)->value);
                label = xstrdup(pname);
            }
            Node *pr = node_new(N_PARAM, kw->line, kw->col);
            pr->name = pname;
            pr->sval = label;
            if (match_op(p, ":")) pr->type = parse_type(p);
            list_push(n->params, pr);
            if (!match_op(p, ",")) break;
            if (check_op(p, ")")) break;
        }
    }
    expect_op(p, ")");
    expect_op(p, "{");
    n->body = list_new();
    while (!check_op(p, "}")) {
        if (check_eof(p)) {
            Token *g = peek(p, 0);
            p_err(p, g->line, g->col, "Незакрытый init");
        }
        list_push(n->body, parse_stmt(p));
    }
    expect_op(p, "}");
    return n;
}

static Node *parse_struct(P *p) {
    Token *kw = expect_kw(p, "struct");
    Token *id = expect_id(p);
    Node *n = node_new(N_STRUCT, kw->line, kw->col);
    n->name = xstrdup(id->value);
    n->fields = list_new();
    n->methods = list_new();
    expect_op(p, "{");
    while (!check_op(p, "}")) {
        if (check_eof(p)) {
            Token *g = peek(p, 0);
            p_err(p, g->line, g->col, "Незакрытая структура");
        }
        if (check_kw(p, "let") || check_kw(p, "var")) {
            Token *fkw = next_tok(p);
            Token *fid = expect_id(p);
            expect_op(p, ":");
            Node *fld = node_new(N_FIELD, fkw->line, fkw->col);
            fld->sval = xstrdup(fkw->value);
            fld->name = xstrdup(fid->value);
            fld->type = parse_type(p);
            list_push(n->fields, fld);
            match_op(p, ";");
        } else if (check_kw(p, "func")) {
            list_push(n->methods, parse_func(p));
        } else {
            Token *g = peek(p, 0);
            p_err(p, g->line, g->col,
                  "В структуре ожидалось поле или метод, получено '%s'",
                  g->value ? g->value : "<eof>");
        }
    }
    expect_op(p, "}");
    return n;
}

static Node *parse_enum(P *p) {
    Token *kw = expect_kw(p, "enum");
    Token *id = expect_id(p);
    Node *n = node_new(N_ENUM, kw->line, kw->col);
    n->name = xstrdup(id->value);
    n->cases = list_new();
    expect_op(p, "{");
    while (!check_op(p, "}")) {
        if (check_eof(p)) {
            Token *g = peek(p, 0);
            p_err(p, g->line, g->col, "Незакрытый enum");
        }
        if (!match_kw(p, "case")) {
            Token *g = peek(p, 0);
            p_err(p, g->line, g->col,
                  "В enum ожидался case, получено '%s'",
                  g->value ? g->value : "<eof>");
        }
        Token *cid = expect_id(p);
        Node *c = node_new(N_ENUM_CASE_DECL, cid->line, cid->col);
        c->name = xstrdup(cid->value);
        c->params = list_new();
        if (match_op(p, "(")) {
            if (!check_op(p, ")")) {
                for (;;) {
                    const char *plabel = NULL, *pname = NULL;
                    if (check_op(p, "_")) {
                        next_tok(p);
                        pname = xstrdup(expect_id(p)->value);
                    } else if (check_id_tok(p) && peek(p, 1)->type == T_ID) {
                        plabel = xstrdup(next_tok(p)->value);
                        pname = xstrdup(expect_id(p)->value);
                    } else {
                        pname = xstrdup(expect_id(p)->value);
                        plabel = xstrdup(pname);
                    }
                    Node *pr = node_new(N_PARAM, cid->line, cid->col);
                    pr->name = pname;
                    pr->sval = plabel;
                    if (match_op(p, ":")) pr->type = parse_type(p);
                    list_push(c->params, pr);
                    if (!match_op(p, ",")) break;
                }
            }
            expect_op(p, ")");
        }
        list_push(n->cases, c);
        match_op(p, ";");
    }
    expect_op(p, "}");
    return n;
}

static Node *parse_class(P *p) {
    Token *kw = expect_kw(p, "class");
    Token *id = expect_id(p);
    Node *n = node_new(N_CLASS, kw->line, kw->col);
    n->name = xstrdup(id->value);
    n->fields = list_new();
    n->methods = list_new();

    if (match_kw(p, "extends")) {
        Token *base = expect_id(p);
        n->sval = xstrdup(base->value);
    }

    if (match_kw(p, "implements")) {
        n->items = list_new();
        while (1) {
            Token *iface = expect_id(p);
            Node *i = node_new(N_ID, iface->line, iface->col);
            i->name = xstrdup(iface->value);
            list_push(n->items, i);
            if (!match_op(p, ",")) break;
        }
    }

    expect_op(p, "{");
    while (!check_op(p, "}")) {
        if (check_eof(p)) {
            Token *g = peek(p, 0);
            p_err(p, g->line, g->col, "Незакрытый class");
        }
        if (check_kw(p, "let") || check_kw(p, "var")) {
            Token *fkw = next_tok(p);
            Token *fid = expect_id(p);
            expect_op(p, ":");
            Node *fld = node_new(N_FIELD, fkw->line, fkw->col);
            fld->sval = xstrdup(fkw->value);
            fld->name = xstrdup(fid->value);
            fld->type = parse_type(p);
            if (match_op(p, "=")) fld->a = parse_expr(p, 0, 0);
            match_op(p, ";");
            list_push(n->fields, fld);
        } else if (check_kw(p, "init")) {
            list_push(n->methods, parse_init(p));
        } else if (check_kw(p, "override")) {
            next_tok(p);
            if (!check_kw(p, "func")) {
                Token *g = peek(p, 0);
                p_err(p, g->line, g->col, "После override ожидался func");
            }
            list_push(n->methods, parse_func(p));
        } else if (check_kw(p, "func")) {
            list_push(n->methods, parse_func(p));
        } else {
            Token *g = peek(p, 0);
            p_err(p, g->line, g->col,
                  "В class ожидалось поле, init, func или override, получено '%s'",
                  g->value ? g->value : "<eof>");
        }
    }
    expect_op(p, "}");
    return n;
}

static Node *parse_pattern(P *p) {
    if (check_op(p, ".")) {
        Token *dot = next_tok(p);
        Token *id = expect_id(p);
        Node *pat = node_new(N_PAT_ENUM, dot->line, dot->col);
        pat->name = xstrdup(id->value);
        pat->bindings = list_new();
        if (match_op(p, "(")) {
            if (!check_op(p, ")")) {
                for (;;) {
                    if (match_kw(p, "let")) {
                        Token *bid = expect_id(p);
                        Node *b = node_new(N_PAT_BIND, bid->line, bid->col);
                        b->name = xstrdup(bid->value);
                        list_push(pat->bindings, b);
                    } else if (check_op(p, "_")) {
                        next_tok(p);
                        Node *b = node_new(N_PAT_BIND, dot->line, dot->col);
                        b->name = NULL;
                        list_push(pat->bindings, b);
                    } else {
                        Token *bid = expect_id(p);
                        Node *b = node_new(N_PAT_BIND, bid->line, bid->col);
                        b->name = xstrdup(bid->value);
                        list_push(pat->bindings, b);
                    }
                    if (!match_op(p, ",")) break;
                }
            }
            expect_op(p, ")");
        }
        return pat;
    }
    if (match_kw(p, "let")) {
        Token *id = expect_id(p);
        Node *b = node_new(N_PAT_BIND, id->line, id->col);
        b->name = xstrdup(id->value);
        return b;
    }
    if (check_op(p, "_")) { next_tok(p); return node_new(N_PAT_WILD, 1, 1); }

    if (match_kw(p, "true"))  return lit_bool(1, 1, 1);
    if (match_kw(p, "false")) return lit_bool(0, 1, 1);
    if (match_kw(p, "nil"))   return lit_null(1, 1);

    Node *e = parse_expr(p, 0, 0);
    if (e->kind == N_LIT) {
        Node *pat = node_new(N_PAT_LIT, e->line, e->col);
        pat->num = e->num; pat->boolean = e->boolean;
        pat->isnull = e->isnull; pat->sval = e->sval;
        return pat;
    }
    if (e->kind == N_BIN && e->op &&
        (strcmp(e->op, "...") == 0 || strcmp(e->op, "..<") == 0)) {
        Node *pat = node_new(N_PAT_RANGE, e->line, e->col);
        pat->a = e->a; pat->b = e->b;
        pat->inclusive = strcmp(e->op, "...") == 0;
        return pat;
    }
    Node *pat = node_new(N_PAT_EXPR, e->line, e->col);
    pat->a = e;
    return pat;
}

static Node *parse_switch(P *p) {
    Token *kw = expect_kw(p, "switch");
    Node *n = node_new(N_SWITCH, kw->line, kw->col);
    n->a = parse_expr(p, 0, 1);   /* subject */
    expect_op(p, "{");
    n->cases = list_new();
    while (!check_op(p, "}")) {
        if (check_eof(p)) {
            Token *g = peek(p, 0);
            p_err(p, g->line, g->col, "Незакрытый switch");
        }
        if (match_kw(p, "default")) {
            expect_op(p, ":");
            List *body = list_new();
            while (!check_kw(p, "case") && !check_kw(p, "default") && !check_op(p, "}")) {
                if (check_eof(p)) { Token *g = peek(p,0); p_err(p,g->line,g->col,"Незакрытый switch"); }
                list_push(body, parse_stmt(p));
            }
            n->body = body;  /* defaultBody */
        } else if (match_kw(p, "case")) {
            Node *sc = node_new(N_SWITCH_CASE, kw->line, kw->col);
            sc->patterns = list_new();
            list_push(sc->patterns, parse_pattern(p));
            while (match_op(p, ",")) list_push(sc->patterns, parse_pattern(p));
            if (match_kw(p, "where")) sc->a = parse_expr(p, 0, 0);
            expect_op(p, ":");
            sc->body = list_new();
            while (!check_kw(p, "case") && !check_kw(p, "default") && !check_op(p, "}")) {
                if (check_eof(p)) { Token *g = peek(p,0); p_err(p,g->line,g->col,"Незакрытый switch"); }
                list_push(sc->body, parse_stmt(p));
            }
            list_push(n->cases, sc);
        } else {
            Token *g = peek(p, 0);
            p_err(p, g->line, g->col,
                  "В switch ожидался case или default, получено '%s'",
                  g->value ? g->value : "<eof>");
        }
    }
    expect_op(p, "}");
    return n;
}

static Node *parse_block(P *p) {
    expect_op(p, "{");
    Node *n = node_new(N_BLOCK, 1, 1);
    n->stmts = list_new();
    while (!check_op(p, "}")) {
        if (check_eof(p)) { Token *g = peek(p,0); p_err(p,g->line,g->col,"Незакрытый блок"); }
        list_push(n->stmts, parse_stmt(p));
    }
    expect_op(p, "}");
    return n;
}

static Node *parse_while(P *p) {
    Token *kw = expect_kw(p, "while");
    Node *n = node_new(N_WHILE, kw->line, kw->col);
    n->a = parse_expr(p, 0, 1);
    n->b = parse_block(p);
    return n;
}

static Node *parse_for(P *p) {
    Token *kw = expect_kw(p, "for");
    Token *id = expect_id(p);
    expect_kw(p, "in");
    Node *n = node_new(N_FOR, kw->line, kw->col);
    n->name = xstrdup(id->value);
    n->a = parse_expr(p, 0, 1);
    n->b = parse_block(p);
    return n;
}

static Node *parse_if(P *p) {
    Token *kw = expect_kw(p, "if");

    /* if let / if var — N_IF_LET */
    if (check_kw(p, "let") || check_kw(p, "var")) {
        Node *n = node_new(N_IF_LET, kw->line, kw->col);
        n->items = list_new();
        while (1) {
            if (check_kw(p, "let") || check_kw(p, "var"))
                list_push(n->items, parse_one_binding(p));
            else {
                n->a = parse_expr(p, 0, 1);
                break;
            }
            if (!check_op(p, ",")) break;
            next_tok(p);
        }
        n->b = parse_block(p);
        if (match_kw(p, "else")) {
            if (check_kw(p, "if")) n->c = parse_if(p);
            else                   n->c = parse_block(p);
        }
        return n;
    }

    /* if */
    Node *n = node_new(N_IF, kw->line, kw->col);
    n->a = parse_expr(p, 0, 1);
    n->b = parse_block(p);
    if (match_kw(p, "else")) {
        if (check_kw(p, "if")) n->c = parse_if(p);
        else                   n->c = parse_block(p);
    }
    return n;
}

static Node *parse_guard(P *p) {
    Token *kw = expect_kw(p, "guard");
    Node *n = node_new(N_GUARD_LET, kw->line, kw->col);
    n->items = list_new();
    while (1) {
        if (check_kw(p, "let") || check_kw(p, "var"))
            list_push(n->items, parse_one_binding(p));
        else {
            n->a = parse_expr(p, 0, 1);
            break;
        }
        if (!check_op(p, ",")) break;
        next_tok(p);
    }
    expect_kw(p, "else");
    n->b = parse_block(p);
    return n;
}

static Node *parse_try(P *p) {
    Token *kw = expect_kw(p, "try");
    if (!check_op(p, "{")) {
        p_err(p, kw->line, kw->col, "После try ожидался блок {");
    }
    Node *n = node_new(N_TRY, kw->line, kw->col);
    n->a = parse_block(p);
    if (!match_kw(p, "catch")) {
        p_err(p, kw->line, kw->col, "Ожидался catch после try");
    }
    if (check_id_tok(p)) {
        Token *id = next_tok(p);
        n->name = xstrdup(id->value);
    }
    n->b = parse_block(p);
    return n;
}

static Node *parse_throw(P *p) {
    Token *kw = expect_kw(p, "throw");
    Node *n = node_new(N_THROW, kw->line, kw->col);
    n->a = parse_expr(p, 0, 0);
    match_op(p, ";");
    return n;
}

static Node *parse_async_func(P *p) {
    Token *kw = expect_kw(p, "async");
    Node *n = parse_func(p);
    n->is_async = 1;
    n->line = kw->line;
    n->col = kw->col;
    return n;
}

static Node *parse_stmt(P *p) {
    if (check_kw(p, "import")) return parse_import(p);
    if (check_kw(p, "export")) return parse_export(p);
    if (check_kw(p, "try"))   return parse_try(p);
    if (check_kw(p, "throw")) return parse_throw(p);
    if (check_kw(p, "async") &&
        peek(p, 1)->type == T_KW && peek(p, 1)->value &&
        strcmp(peek(p, 1)->value, "func") == 0)
        return parse_async_func(p);
    if (check_kw(p, "let") || check_kw(p, "var")) return parse_let(p);
    if (check_kw(p, "func")) return parse_func(p);
    if (check_kw(p, "struct")) return parse_struct(p);
    if (check_kw(p, "enum")) return parse_enum(p);
    if (check_kw(p, "class")) return parse_class(p);
    if (check_kw(p, "switch")) {
        Node *n = node_new(N_EXPR_STMT, peek(p,0)->line, peek(p,0)->col);
        n->a = parse_switch(p);
        return n;
    }
    if (check_kw(p, "return")) {
        Token *kw = next_tok(p);
        Node *n = node_new(N_RETURN, kw->line, kw->col);
        if (check_op(p, "}") || check_eof(p) || check_op(p, ";")) {
            match_op(p, ";");
            return n;
        }
        n->a = parse_expr(p, 0, 0);
        match_op(p, ";");
        return n;
    }
    if (check_kw(p, "if")) {
        Node *n = node_new(N_EXPR_STMT, peek(p,0)->line, peek(p,0)->col);
        n->a = parse_if(p);
        return n;
    }
    if (check_kw(p, "guard")) return parse_guard(p);
    if (check_kw(p, "while")) return parse_while(p);
    if (check_kw(p, "for"))   return parse_for(p);
    if (check_kw(p, "break")) {
        Token *kw = next_tok(p);
        match_op(p, ";");
        return node_new(N_BREAK, kw->line, kw->col);
    }
    if (check_kw(p, "continue")) {
        Token *kw = next_tok(p);
        match_op(p, ";");
        return node_new(N_CONTINUE, kw->line, kw->col);
    }
    if (check_op(p, "{")) return parse_block(p);

    Token *start = peek(p, 0);
    Node *n = node_new(N_EXPR_STMT, start->line, start->col);
    n->a = parse_expr(p, 0, 0);
    match_op(p, ";");
    return n;
}

static const struct { const char *op; double prec; } PREC[] = {
    {"??",1}, {"||",2}, {"&&",3}, {"==",4}, {"!=",4},
    {"<",5}, {"<=",5}, {">",5}, {">=",5},
    {"..<",5.5}, {"...",5.5},
    {"+",6}, {"-",6}, {"*",7}, {"/",7}, {"%",7},
    {NULL,0}
};
static double get_prec(const char *op) {
    for (int i = 0; PREC[i].op; i++) if (strcmp(PREC[i].op, op) == 0) return PREC[i].prec;
    return -1;
}

static int is_lvalue(Node *n) {
    if (!n) return 0;
    switch (n->kind) {
    case N_ID:
    case N_MEMBER:
    case N_INDEX:
        return 1;
    default:
        return 0;
    }
}

static int is_assign_op(const char *op) {
    static const char *OPS[] = {"+=","-=","*=","/=","%=", NULL};
    for (int i = 0; OPS[i]; i++) if (strcmp(OPS[i], op) == 0) return 1;
    return 0;
}

static Node *parse_unary(P *p, int no_trailing);

static List *parse_args(P *p) {
    List *args = list_new();
    if (!check_op(p, ")")) {
        for (;;) {
            const char *name = NULL;
            if (check_id_tok(p) && peek(p,1)->type == T_OP &&
                peek(p,1)->value && strcmp(peek(p,1)->value, ":") == 0) {
                name = xstrdup(next_tok(p)->value);
                next_tok(p);
            }
            Node *arg = node_new(N_CALL_ARG, 1, 1);
            arg->name = name;
            arg->a = parse_expr(p, 0, 0);
            list_push(args, arg);
            if (!match_op(p, ",")) break;
            if (check_op(p, ")")) break;
        }
    }
    return args;
}

static Node *parse_postfix(P *p, int no_trailing) {
    int is_new = 0;
    if (check_kw(p, "new")) {
        next_tok(p);
        is_new = 1;
    }
    Node *node = NULL;
    /* parsePrimary inline */
    {
        Token *t = peek(p, 0);
        if (t->type == T_NUM) {
            next_tok(p);
            node = lit_num(t->num, t->line, t->col);
        } else if (t->type == T_STR) {
            next_tok(p);
            /* handle interpolation */
            const char *raw = t->value;
            if (!strstr(raw, "\\(")) {
                /* unescape in place */
                StrBuf sb; sb_init(&sb);
                for (size_t i = 0; raw[i]; ) {
                    if (raw[i] == '\\' && raw[i+1]) {
                        char n = raw[i+1];
                        if (n == 'n') sb_putc(&sb, '\n');
                        else if (n == 't') sb_putc(&sb, '\t');
                        else if (n == 'r') sb_putc(&sb, '\r');
                        else if (n == '\\') sb_putc(&sb, '\\');
                        else if (n == '"') sb_putc(&sb, '"');
                        else if (n == '\'') sb_putc(&sb, '\'');
                        else sb_putc(&sb, n);
                        i += 2;
                    } else sb_putc(&sb, raw[i++]);
                }
                node = lit_str(sb_take(&sb), t->line, t->col);
            } else {
                /* template */
                node = node_new(N_TEMPLATE, t->line, t->col);
                node->parts = list_new();
                StrBuf text; sb_init(&text);
                size_t i = 0;
                while (raw[i]) {
                    if (raw[i] == '\\' && raw[i+1] == '(') {
                        if (text.len) {
                            Node *pt = node_new(N_TEMPLATE_PART, t->line, t->col);
                            pt->sval = xstrdup("text");
                            pt->name = sb_take(&text);
                            sb_init(&text);
                            list_push(node->parts, pt);
                        }
                        i += 2;
                        int depth = 1; size_t j = i;
                        while (raw[j] && depth > 0) {
                            if (raw[j] == '(') depth++;
                            else if (raw[j] == ')') depth--;
                            if (depth == 0) break;
                            j++;
                        }
                        char *sub = xstrndup(raw + i, j - i);
                        /* recursive tokenize+parse */
                        SSError serr = {0};
                        TokenList *subtoks = tokenize(sub, &serr);
                        free(sub);
                        if (!subtoks) {
                            p_err(p, t->line, t->col, "Ошибка в интерполяции: %s", serr.msg);
                        }
                        /* recursively parse as sub-program */
                        P sub_p = {0};
                        sub_p.toks = subtoks;
                        sub_p.jb_top = 1;
                        Node *sub_ast = NULL;
                        if (setjmp(sub_p.jbs[0]) == 0) {
                            sub_ast = parse_program(&sub_p);
                        } else {
                            free_tokens(subtoks);
                            p_err(p, t->line, t->col, "Ошибка в интерполяции: %s", sub_p.err_msg);
                        }
                        free_tokens(subtoks);
                        if (!sub_ast || sub_ast->stmts->count != 1 ||
                            ((Node*)sub_ast->stmts->items[0])->kind != N_EXPR_STMT) {
                            p_err(p, t->line, t->col, "Интерполяция должна быть выражением");
                        }
                        Node *expr = ((Node*)sub_ast->stmts->items[0])->a;
                        Node *pt = node_new(N_TEMPLATE_PART, t->line, t->col);
                        pt->sval = xstrdup("expr");
                        pt->a = expr;
                        list_push(node->parts, pt);
                        i = j + 1;
                    } else if (raw[i] == '\\' && raw[i+1]) {
                        char n = raw[i+1];
                        if (n == 'n') sb_putc(&text, '\n');
                        else if (n == 't') sb_putc(&text, '\t');
                        else if (n == 'r') sb_putc(&text, '\r');
                        else if (n == '\\') sb_putc(&text, '\\');
                        else sb_putc(&text, n);
                        i += 2;
                    } else sb_putc(&text, raw[i++]);
                }
                if (text.len) {
                    Node *pt = node_new(N_TEMPLATE_PART, t->line, t->col);
                    pt->sval = xstrdup("text");
                    pt->name = sb_take(&text);
                    list_push(node->parts, pt);
                } else sb_free(&text);
            }
        } else if (match_kw(p, "true"))  node = lit_bool(1, t->line, t->col);
        else if (match_kw(p, "false"))   node = lit_bool(0, t->line, t->col);
        else if (match_kw(p, "nil"))     node = lit_null(t->line, t->col);
        else if (check_kw(p, "if"))      node = parse_if(p);
        else if (check_kw(p, "switch"))  node = parse_switch(p);
        else if (check_op(p, ".")) {
            next_tok(p);
            Token *id = expect_id(p);
            node = node_new(N_ENUM_CASE, t->line, t->col);
            node->name = xstrdup(id->value);
        } else if (check_op(p, "[")) {
            next_tok(p);
            Node *arr = node_new(N_ARRAY, t->line, t->col);
            arr->items = list_new();
            Node *dict = node_new(N_DICT, t->line, t->col);
            dict->pairs = list_new();
            int is_dict = 0;
            if (!check_op(p, "]")) {
                for (;;) {
                    Node *first = parse_expr(p, 0, 0);
                    if (check_op(p, ":")) {
                        next_tok(p);
                        Node *pair = node_new(N_DICT_PAIR, first->line, first->col);
                        pair->a = first;
                        pair->b = parse_expr(p, 0, 0);
                        list_push(dict->pairs, pair);
                        is_dict = 1;
                    } else list_push(arr->items, first);
                    if (!match_op(p, ",")) break;
                    if (check_op(p, "]")) break;
                }
            }
            expect_op(p, "]");
            node = is_dict ? dict : arr;
        } else if (check_op(p, "(")) {
            next_tok(p);
            Node *tuple = node_new(N_TUPLE, t->line, t->col);
            tuple->items = list_new();
            if (!check_op(p, ")")) {
                for (;;) {
                    if (check_id_tok(p) && peek(p,1)->type == T_OP &&
                        peek(p,1)->value && strcmp(peek(p,1)->value, ":") == 0) {
                        next_tok(p); next_tok(p);
                    }
                    list_push(tuple->items, parse_expr(p, 0, 0));
                    if (!match_op(p, ",")) break;
                }
            }
            expect_op(p, ")");
            if (tuple->items->count == 1) node = tuple->items->items[0];
            else node = tuple;
        } else if (check_op(p, "{")) {
            node = parse_closure(p);
                } else if (check_kw(p, "func")) {
            next_tok(p);
            expect_op(p, "(");
            List *params = list_new();
            if (!check_op(p, ")")) {
                for (;;) {
                    if (check_op(p, "_")) next_tok(p);
                    Token *pid = expect_id(p);
                    Node *pr = node_new(N_PARAM, pid->line, pid->col);
                    pr->name = xstrdup(pid->value);
                    if (match_op(p, ":")) pr->type = parse_type(p);
                    list_push(params, pr);
                    if (!match_op(p, ",")) break;
                }
            }
            expect_op(p, ")");
            int is_async_fn = match_kw(p, "async");
            if (match_op(p, "->")) parse_type(p);
            expect_op(p, "{");
            List *body = list_new();
            while (!check_op(p, "}")) list_push(body, parse_stmt(p));
            expect_op(p, "}");
            node = node_new(N_CLOSURE, t->line, t->col);
            node->params = params;
            node->body = body;
            node->is_async = is_async_fn;
        } else if (t->type == T_ID) {
            next_tok(p);
            node = id_node(t->value, t->line, t->col);
        } else if (t->type == T_KW && t->value && strcmp(t->value, "self") == 0) {
            next_tok(p);
            node = id_node("self", t->line, t->col);
        } else if (t->type == T_KW && t->value && strcmp(t->value, "super") == 0) {
            next_tok(p);
            node = id_node("super", t->line, t->col);
        } else {
            p_err(p, t->line, t->col, "Неожиданный токен '%s'",
                  t->value ? t->value : "<eof>");
        }
    }

    if (is_new) {
        while (1) {
            if (check_op(p, ".")) {
                next_tok(p);
                Token *id = expect_id(p);
                Node *m = node_new(N_MEMBER, id->line, id->col);
                m->a = node; m->name = xstrdup(id->value); m->optional = 0;
                node = m;
            } else if (check_op(p, "?.")) {
                next_tok(p);
                Token *id = expect_id(p);
                Node *m = node_new(N_MEMBER, id->line, id->col);
                m->a = node; m->name = xstrdup(id->value); m->optional = 1;
                node = m;
            } else if (check_op(p, "(")) {
                next_tok(p);
                Node *c = node_new(N_CALL, 1, 1);
                c->a = node;
                c->args = parse_args(p);
                expect_op(p, ")");
                c->is_new = 1;
                node = c;
                break;
            } else {
                Token *g = peek(p, 0);
                p_err(p, g->line, g->col, "Ожидался вызов (...) после new");
            }
        }
    }

    while (1) {
        if (check_op(p, ".")) {
            next_tok(p);
            Token *id = expect_member_name(p);
            Node *m = node_new(N_MEMBER, id->line, id->col);
            m->a = node;
            m->name = xstrdup(id->value);
            m->optional = 0;
            node = m;
        } else if (check_op(p, "?.")) {
            next_tok(p);
            Token *id = expect_member_name(p);
            Node *m = node_new(N_MEMBER, id->line, id->col);
            m->a = node;
            m->name = xstrdup(id->value);
            m->optional = 1;
            node = m;
        } else if (check_op(p, "(")) {
            next_tok(p);
            Node *c = node_new(N_CALL, 1, 1);
            c->a = node;
            c->args = parse_args(p);
            expect_op(p, ")");
            node = c;
        } else if (check_op(p, "[")) {
            next_tok(p);
            Node *ix = node_new(N_INDEX, 1, 1);
            ix->a = node;
            ix->b = parse_expr(p, 0, 0);
            expect_op(p, "]");
            node = ix;
        } else if (check_op(p, "!")) {
            next_tok(p);
            Node *f = node_new(N_FORCE, 1, 1);
            f->a = node;
            node = f;
        } else if (check_op(p, "{") && !no_trailing) {
            Node *cl = parse_closure(p);
            Node *arg = node_new(N_CALL_ARG, 1, 1);
            arg->name = NULL;
            arg->a = cl;
            if (node->kind == N_CALL) {
                list_push(node->args, arg);
            } else {
                Node *c = node_new(N_CALL, 1, 1);
                c->a = node;
                c->args = list_new();
                list_push(c->args, arg);
                node = c;
            }
        } else break;
    }
    return node;
}

static Node *parse_unary(P *p, int no_trailing) {
    Token *t = peek(p, 0);
    if (t->type == T_OP && t->value &&
        (strcmp(t->value,"-")==0 || strcmp(t->value,"!")==0 || strcmp(t->value,"+")==0)) {
        next_tok(p);
        Node *n = node_new(N_UNARY, t->line, t->col);
        n->op = xstrdup(t->value);
        n->a = parse_unary(p, no_trailing);
        return n;
    }
    if (check_kw(p, "await")) {
        next_tok(p);
        Node *n = node_new(N_AWAIT, t->line, t->col);
        n->a = parse_unary(p, no_trailing);
        return n;
    }
    return parse_postfix(p, no_trailing);
}

static Node *parse_expr(P *p, double min_prec, int no_trailing) {
    Node *left = parse_unary(p, no_trailing);
    while (1) {
        Token *t = peek(p, 0);
        if (t->type != T_OP || !t->value) break;
        double prec = get_prec(t->value);
        if (prec < 0 || prec < min_prec) break;
        next_tok(p);
        Node *right = parse_expr(p, prec + 0.01, no_trailing);
        Node *b = node_new(N_BIN, t->line, t->col);
        b->op = xstrdup(t->value);
        b->a = left;
        b->b = right;
        left = b;
    }
    if (min_prec == 0 && check_op(p, "?")) {
        Token *q = next_tok(p);
        Node *n = node_new(N_TERNARY, q->line, q->col);
        n->a = left;
        n->b = parse_expr(p, 0, 0);
        expect_op(p, ":");
        n->c = parse_expr(p, 0, 0);
        left = n;
    }
    if (min_prec == 0 && check_op(p, "=")) {
        Token *eq = next_tok(p);
        if (!is_lvalue(left)) {
            p_err(p, eq->line, eq->col,
                  "Левый операнд присваивания не является lvalue");
        }
        Node *n = node_new(N_ASSIGN, eq->line, eq->col);
        n->a = left;
        n->op = xstrdup("=");
        n->b = parse_expr(p, 0, 0);
        left = n;
    } else if (min_prec == 0 && peek(p,0)->type == T_OP && peek(p,0)->value &&
               is_assign_op(peek(p,0)->value)) {
        Token *eq = next_tok(p);
        if (!is_lvalue(left)) {
            p_err(p, eq->line, eq->col,
                  "Левый операнд присваивания не является lvalue");
        }
        Node *n = node_new(N_ASSIGN, eq->line, eq->col);
        n->a = left;
        n->op = xstrdup(eq->value);
        n->b = parse_expr(p, 0, 0);
        left = n;
    }
    return left;
}

static Node *parse_closure(P *p) {
    expect_op(p, "{");
    int is_async_cl = match_kw(p, "async");
    int save = p->pos;
    List *params = NULL;

    if (check_op(p, "(")) {
        int jb_save = p->jb_top;
        p->jb_top++;
        if (setjmp(p->jbs[p->jb_top - 1]) == 0) {
            next_tok(p);
            List *ps = list_new();
            if (!check_op(p, ")")) {
                for (;;) {
                    if (check_op(p, "_")) next_tok(p);
                    Token *pid = expect_id(p);
                    Node *pr = node_new(N_PARAM, pid->line, pid->col);
                    pr->name = xstrdup(pid->value);
                    if (match_op(p, ":")) pr->type = parse_type(p);
                    list_push(ps, pr);
                    if (!match_op(p, ",")) break;
                }
            }
            expect_op(p, ")");
            if (match_op(p, "->")) parse_type(p);
            if (match_kw(p, "in")) {
                params = ps;
                p->jb_top = jb_save;
            } else {
                p->pos = save;
                p->jb_top = jb_save;
            }
        } else {
            p->pos = save;
            p->jb_top = jb_save;
            p->has_err = 0;
        }
    }

    if (!params && check_id_tok(p) &&
        ((peek(p,1)->type == T_KW && peek(p,1)->value &&
          strcmp(peek(p,1)->value, "in") == 0) ||
         (peek(p,1)->type == T_OP && peek(p,1)->value &&
          strcmp(peek(p,1)->value, ",") == 0))) {
        int try_save = p->pos;
        int jb_save = p->jb_top;
        p->jb_top++;
        if (setjmp(p->jbs[p->jb_top - 1]) == 0) {
            List *ps = list_new();
            Node *pr = node_new(N_PARAM, 1, 1);
            pr->name = xstrdup(expect_id(p)->value);
            list_push(ps, pr);
            while (match_op(p, ",")) {
                Node *pr2 = node_new(N_PARAM, 1, 1);
                pr2->name = xstrdup(expect_id(p)->value);
                list_push(ps, pr2);
            }
            if (match_kw(p, "in")) {
                params = ps;
                p->jb_top = jb_save;
            } else {
                p->pos = try_save;
                p->jb_top = jb_save;
            }
        } else {
            p->pos = try_save;
            p->jb_top = jb_save;
            p->has_err = 0;
        }
    }

    Node *n = node_new(N_CLOSURE, 1, 1);
    n->params = params;
    n->body = list_new();
    n->is_async = is_async_cl;
    while (!check_op(p, "}")) {
        if (check_eof(p)) { Token *g = peek(p,0); p_err(p,g->line,g->col,"Незакрытое замыкание"); }
        list_push(n->body, parse_stmt(p));
    }
    expect_op(p, "}");
    return n;
}

Node *parse(TokenList *toks, SSError *err) {
    P p;
    memset(&p, 0, sizeof(p));
    p.toks = toks;
    p.pos = 0;
    p.jb_top = 1;
    Node *root = NULL;
    if (setjmp(p.jbs[0]) == 0) {
        root = parse_program(&p);
    } else {
        if (err) {
            err->line = p.err_line;
            err->col = p.err_col;
            snprintf(err->msg, sizeof(err->msg), "%s", p.err_msg);
        }
        return NULL;
    }
    return root;
}