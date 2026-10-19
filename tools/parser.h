#ifndef PARSER_H
#define PARSER_H

#include "util.h"
#include "ast.h"
#include "lexer.h"

Node *parse(TokenList *toks, SSError *err);

#endif
