#ifndef TYPECHECK_H
#define TYPECHECK_H

#include "ast.h"

int typecheck_program(Node *prog);

int         tc_had_error(void);
const char *tc_error_msg(void);
int         tc_error_line(void);
int         tc_error_col(void);

#endif