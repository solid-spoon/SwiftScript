#include "ast.h"
#include <string.h>
#include <stdlib.h>

List *list_new(void) {
    List *l = xcalloc(1, sizeof(List));
    return l;
}

void list_push(List *l, void *item) {
    if (l->count == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 8;
        l->items = xrealloc(l->items, sizeof(void *) * l->cap);
    }
    l->items[l->count++] = item;
}

Node *node_new(NodeKind k, int line, int col) {
    Node *n = xcalloc(1, sizeof(Node));
    n->kind = k;
    n->line = line;
    n->col = col;
    return n;
}

Node *lit_num(double v, int line, int col) {
    Node *n = node_new(N_LIT, line, col);
    n->num = v;
    return n;
}
Node *lit_bool(int v, int line, int col) {
    Node *n = node_new(N_LIT, line, col);
    n->boolean = v ? 1 : 0;
    n->is_bool = 1;
    return n;
}
Node *lit_null(int line, int col) {
    Node *n = node_new(N_LIT, line, col);
    n->isnull = 1;
    return n;
}
Node *lit_str(const char *s, int line, int col) {
    Node *n = node_new(N_LIT, line, col);
    n->sval = s ? xstrdup(s) : NULL;
    return n;
}
Node *id_node(const char *name, int line, int col) {
    Node *n = node_new(N_ID, line, col);
    n->name = xstrdup(name);
    return n;
}