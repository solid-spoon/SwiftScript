#ifndef LEXER_H
#define LEXER_H

#include "util.h"

typedef enum {
    T_EOF, T_NUM, T_STR, T_KW, T_ID, T_OP
} TokType;

typedef struct {
    TokType type;
    char   *value;
    double  num;
    int     line, col;
} Token;

typedef struct {
    Token *items;
    int    count;
    int    cap;
} TokenList;

TokenList *tokenize(const char *src, SSError *err);
void       free_tokens(TokenList *t);

#endif
