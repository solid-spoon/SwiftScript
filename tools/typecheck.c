#include "typecheck.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

typedef enum {
    TY_UNKNOWN, TY_VOID, TY_ANY, TY_INT, TY_DOUBLE, TY_STRING, TY_BOOL, TY_NIL,
    TY_ARRAY, TY_OPTIONAL, TY_NAMED
} TypeKind;

typedef struct Type Type;
struct Type {
    TypeKind kind;
    const char *name;
    Type *inner;
};

typedef struct Symbol Symbol;
struct Symbol {
    const char *name;
    Type *type;
    Symbol *next;
};

typedef struct Scope {
    Symbol *symbols;
    struct Scope *parent;
} Scope;

typedef struct ProtocolDef ProtocolDef;
struct ProtocolDef {
    const char *name;
    Node *node;
    ProtocolDef *next;
};
static ProtocolDef *_protocols = NULL;

typedef struct {
    int has_err;
    int line, col;
    char msg[512];
} TCError;

static TCError _tc;

static void tc_error(int line, int col, const char *fmt, ...) {
    if (_tc.has_err) return;
    _tc.line = line; _tc.col = col;
    va_list ap; va_start(ap, fmt);
    vsnprintf(_tc.msg, sizeof(_tc.msg), fmt, ap);
    va_end(ap);
    _tc.has_err = 1;
}

int         tc_had_error(void) { return _tc.has_err; }
const char *tc_error_msg(void) { return _tc.msg; }
int         tc_error_line(void){ return _tc.line; }
int         tc_error_col(void) { return _tc.col; }

static Type ty_int = { TY_INT, NULL, NULL };
static Type ty_double = { TY_DOUBLE, NULL, NULL };
static Type ty_string = { TY_STRING, NULL, NULL };
static Type ty_bool = { TY_BOOL, NULL, NULL };
static Type ty_nil = { TY_NIL, NULL, NULL };
static Type ty_any = { TY_ANY, NULL, NULL };
static Type ty_void = { TY_VOID, NULL, NULL };
static Type ty_unknown = { TY_UNKNOWN, NULL, NULL };

static Type *type_new_array(Type *inner) {
    Type *t = xmalloc(sizeof(Type));
    t->kind = TY_ARRAY; t->inner = inner; t->name = NULL;
    return t;
}

static Type *type_new_optional(Type *inner) {
    Type *t = xmalloc(sizeof(Type));
    t->kind = TY_OPTIONAL; t->inner = inner; t->name = NULL;
    return t;
}

static Type *type_new_named(const char *name) {
    Type *t = xmalloc(sizeof(Type));
    t->kind = TY_NAMED; t->name = name; t->inner = NULL;
    return t;
}

static Scope *scope_new(Scope *parent) {
    Scope *s = xcalloc(1, sizeof(Scope));
    s->parent = parent;
    return s;
}

static void scope_add(Scope *s, const char *name, Type *type) {
    Symbol *sym = xmalloc(sizeof(Symbol));
    sym->name = name;
    sym->type = type;
    sym->next = s->symbols;
    s->symbols = sym;
}

static Type *scope_find(Scope *s, const char *name) {
    for (Scope *curr = s; curr; curr = curr->parent) {
        for (Symbol *sym = curr->symbols; sym; sym = sym->next) {
            if (strcmp(sym->name, name) == 0) return sym->type;
        }
    }
    return NULL;
}

static void register_protocol(const char *name, Node *node) {
    ProtocolDef *p = xmalloc(sizeof(ProtocolDef));
    p->name = name;
    p->node = node;
    p->next = _protocols;
    _protocols = p;
}

static Node *find_protocol(const char *name) {
    for (ProtocolDef *p = _protocols; p; p = p->next) {
        if (strcmp(p->name, name) == 0) return p->node;
    }
    return NULL;
}

static Type *resolve_type(Node *n) {
    if (!n) return &ty_unknown;
    switch (n->kind) {
    case N_TYPE_NAMED:
        if (strcmp(n->name, "Int") == 0) return &ty_int;
        if (strcmp(n->name, "Double") == 0) return &ty_double;
        if (strcmp(n->name, "String") == 0) return &ty_string;
        if (strcmp(n->name, "Bool") == 0) return &ty_bool;
        if (strcmp(n->name, "Any") == 0) return &ty_any;
        if (strcmp(n->name, "Void") == 0) return &ty_void;
        return type_new_named(n->name);
    case N_TYPE_ARRAY:
        return type_new_array(resolve_type(n->a));
    case N_TYPE_OPT:
        return type_new_optional(resolve_type(n->a));
    default:
        return &ty_unknown;
    }
}

static int is_assignable(Type *to, Type *from) {
    if (to->kind == TY_ANY || from->kind == TY_ANY || to->kind == TY_UNKNOWN || from->kind == TY_UNKNOWN) return 1;
    if (to->kind == TY_OPTIONAL) {
        if (from->kind == TY_NIL) return 1;
        return is_assignable(to->inner, from);
    }
    if (to->kind == TY_DOUBLE && from->kind == TY_INT) return 1;
    if (to->kind != from->kind) return 0;
    if (to->kind == TY_ARRAY) return is_assignable(to->inner, from->inner);
    if (to->kind == TY_NAMED) return strcmp(to->name, from->name) == 0;
    return 1;
}

static Type *check_expr(Node *n, Scope *scope);

static Type *check_call(Node *n, Scope *scope) {
    if (n->a->kind == N_ID) {
        Type *t = scope_find(scope, n->a->name);
        if (t && t->kind == TY_NAMED) return t;
    }
    return &ty_any;
}

static Type *check_expr(Node *n, Scope *scope) {
    if (!n) return &ty_unknown;
    switch (n->kind) {
    case N_LIT:
        if (n->isnull) return &ty_nil;
        if (n->sval) return &ty_string;
        if (n->is_bool) return &ty_bool;
        if (n->num == (long long)n->num) return &ty_int;
        return &ty_double;
    case N_ID: {
        Type *t = scope_find(scope, n->name);
        return t ? t : &ty_any;
    }
    case N_BIN:
        if (strcmp(n->op, "==") == 0 || strcmp(n->op, "!=") == 0 ||
            strcmp(n->op, "<") == 0 || strcmp(n->op, ">") == 0 ||
            strcmp(n->op, "<=") == 0 || strcmp(n->op, ">=") == 0 ||
            strcmp(n->op, "&&") == 0 || strcmp(n->op, "||") == 0) {
            check_expr(n->a, scope); check_expr(n->b, scope);
            return &ty_bool;
        }
        check_expr(n->a, scope); check_expr(n->b, scope);
        return &ty_double;
    case N_CALL: return check_call(n, scope);
    case N_ARRAY: {
        Type *elem_type = &ty_unknown;
        for (int i = 0; i < n->items->count; i++) {
            Type *t = check_expr(n->items->items[i], scope);
            if (elem_type->kind == TY_UNKNOWN) elem_type = t;
        }
        return type_new_array(elem_type);
    }
    default: return &ty_any;
    }
}

static void check_stmt(Node *n, Scope *scope);
static void check_block(List *stmts, Scope *scope) {
    for (int i = 0; i < stmts->count; i++) check_stmt(stmts->items[i], scope);
}

static void check_stmt(Node *n, Scope *scope) {
    if (!n) return;
    switch (n->kind) {
    case N_LET: {
        Type *declared = resolve_type(n->type);
        if (n->a) {
            Type *val = check_expr(n->a, scope);
            if (n->type) {
                if (!is_assignable(declared, val)) {
                    tc_error(n->line, n->col, "Type mismatch: cannot assign value to declared type");
                }
            } else {
                declared = val;
            }
        }
        
        if (n->items && n->items->count > 0) {
            for (int i = 0; i < n->items->count; i++) {
                const char *name = (const char *)n->items->items[i];
                scope_add(scope, name, &ty_any);
            }
        } else {
            if (n->name) scope_add(scope, n->name, declared);
        }
        break;
    }
    case N_ASSIGN: {
        Type *var = check_expr(n->a, scope);
        Type *val = check_expr(n->b, scope);
        if (!is_assignable(var, val)) {
            tc_error(n->line, n->col, "Type mismatch in assignment");
        }
        break;
    }
    case N_FUNC: {
        Scope *func_scope = scope_new(scope);
        for (int i = 0; i < n->params->count; i++) {
            Node *p = n->params->items[i];
            scope_add(func_scope, p->name, resolve_type(p->type));
        }
        check_block(n->body, func_scope);
        break;
    }
    case N_IF: {
        check_expr(n->a, scope);
        check_block(n->b->stmts, scope_new(scope));
        if (n->c) check_stmt(n->c, scope_new(scope));
        break;
    }
    case N_GUARD_LET:
    case N_IF_LET: {
        Scope *inner = scope_new(scope);
        for (int i = 0; i < n->items->count; i++) {
            Node *b = n->items->items[i];
            if (b->items && b->items->count > 0) {
                for (int k = 0; k < b->items->count; k++) {
                    const char *name = (const char *)b->items->items[k];
                    scope_add(inner, name, &ty_any);
                }
            } else if (b->name) {
                scope_add(inner, b->name, &ty_any);
            }
        }
        if (n->b) check_stmt(n->b, inner);
        if (n->c) check_stmt(n->c, scope);
        break;
    }
    case N_FOR: {
        Scope *loop_scope = scope_new(scope);
        if (n->items && n->items->count > 0) {
            for (int i = 0; i < n->items->count; i++) {
                const char *name = (const char *)n->items->items[i];
                scope_add(loop_scope, name, &ty_any);
            }
        } else if (n->name) {
            scope_add(loop_scope, n->name, &ty_any);
        }
        if (n->b) check_stmt(n->b, loop_scope);
        break;
    }
    case N_RETURN: {
        if (n->a) check_expr(n->a, scope);
        break;
    }
    case N_EXPR_STMT: 
        if (n->a) check_expr(n->a, scope); 
        break;
    case N_BLOCK:
        check_block(n->stmts, scope_new(scope));
        break;
    case N_CLASS:
    case N_STRUCT: {
        if (n->items && n->items->count > 0) {
            for (int i = 0; i < n->items->count; i++) {
                Node *proto_ref = n->items->items[i];
                Node *proto_node = find_protocol(proto_ref->name);
                if (!proto_node) {
                    tc_error(n->line, n->col, "Cannot find protocol '%s' in scope", proto_ref->name);
                    continue;
                }
                
                for (int j = 0; j < proto_node->methods->count; j++) {
                    Node *req = proto_node->methods->items[j];
                    int found = 0;
                    for (int k = 0; k < n->methods->count; k++) {
                        Node *impl = n->methods->items[k];
                        if (strcmp(req->name, impl->name) == 0) {
                            Type *req_ret = resolve_type(req->retType);
                            Type *impl_ret = resolve_type(impl->retType);
                            if (!is_assignable(req_ret, impl_ret)) {
                                tc_error(impl->line, impl->col, "Method '%s' return type does not match protocol '%s'", impl->name, proto_ref->name);
                            }
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        tc_error(n->line, n->col, "Type '%s' does not conform to protocol '%s' (missing method '%s')", 
                                 n->name, proto_ref->name, req->name);
                    }
                }

                for (int j = 0; j < proto_node->fields->count; j++) {
                    Node *req = proto_node->fields->items[j];
                    int found = 0;
                    for (int k = 0; k < n->fields->count; k++) {
                        Node *impl = n->fields->items[k];
                        if (strcmp(req->name, impl->name) == 0) {
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        tc_error(n->line, n->col, "Type '%s' does not conform to protocol '%s' (missing property '%s')", 
                                 n->name, proto_ref->name, req->name);
                    }
                }
            }
        }
        
        Scope *class_scope = scope_new(scope);
        for (int i = 0; i < n->fields->count; i++) {
            Node *fld = n->fields->items[i];
            scope_add(class_scope, fld->name, resolve_type(fld->type));
        }
        for (int i = 0; i < n->methods->count; i++) {
            Node *m = n->methods->items[i];
            scope_add(class_scope, m->name, resolve_type(m->retType));
            check_stmt(m, class_scope);
        }
        break;
    }
    default: break;
    }
}

int typecheck_program(Node *prog) {
    memset(&_tc, 0, sizeof(_tc));
    _protocols = NULL;

    Scope *global = scope_new(NULL);

    for (int i = 0; i < prog->stmts->count; i++) {
        Node *s = prog->stmts->items[i];
        if (s->kind == N_PROTOCOL) {
            register_protocol(s->name, s);
            scope_add(global, s->name, type_new_named(s->name));
        } else if (s->kind == N_FUNC) {
            Type *ret = resolve_type(s->retType);
            scope_add(global, s->name, ret);
        } else if (s->kind == N_STRUCT || s->kind == N_CLASS) {
            scope_add(global, s->name, type_new_named(s->name));
        }
    }

    for (int i = 0; i < prog->stmts->count; i++) {
        check_stmt(prog->stmts->items[i], global);
    }

    return !_tc.has_err;
}