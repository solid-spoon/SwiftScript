#ifndef AST_H
#define AST_H

#include "util.h"

typedef enum {
    N_PROGRAM,
    N_LET, N_FUNC, N_STRUCT, N_ENUM, N_RETURN, N_EXPR_STMT,
    N_BLOCK, N_IF, N_WHILE, N_FOR, N_BREAK, N_CONTINUE, N_SWITCH,
    N_IMPORT,
    N_IF_LET, N_GUARD_LET,
    N_LIT, N_ID, N_ENUM_CASE, N_TEMPLATE,
    N_ARRAY, N_DICT, N_TUPLE, N_MEMBER, N_INDEX, N_CALL,
    N_FORCE, N_UNARY, N_BIN, N_TERNARY, N_ASSIGN, N_CLOSURE,
    N_TRY, N_THROW, N_AWAIT,
    N_TYPE_NAMED, N_TYPE_ARRAY, N_TYPE_TUPLE, N_TYPE_OPT,
    N_PAT_ENUM, N_PAT_BIND, N_PAT_WILD, N_PAT_LIT, N_PAT_RANGE, N_PAT_EXPR,
    N_PARAM, N_FIELD, N_ENUM_CASE_DECL, N_SWITCH_CASE,
    N_TEMPLATE_PART, N_CALL_ARG, N_DICT_PAIR
} NodeKind;

typedef struct Node Node;

typedef struct {
    void **items;
    int    count;
    int    cap;
} List;

struct Node {
    NodeKind kind;
    int      line, col;
    int is_bool;
    int is_async;

    const char *name;
    const char *sval;
    const char *op;

    double num;
    int    boolean;
    int    isnull;

    int optional;
    int inclusive;
    int exported;

    Node *a, *b, *c;

    List *stmts;
    List *params;
    List *fields;
    List *methods;
    List *cases;
    List *body;
    List *args;
    List *items;
    List *pairs;
    List *patterns;
    List *bindings;
    List *parts;

    Node *type;
    Node *retType;
};

List *list_new(void);
void  list_push(List *l, void *item);

Node *node_new(NodeKind k, int line, int col);

Node *lit_num(double v, int line, int col);
Node *lit_bool(int v, int line, int col);
Node *lit_null(int line, int col);
Node *lit_str(const char *s, int line, int col);
Node *id_node(const char *name, int line, int col);

#endif