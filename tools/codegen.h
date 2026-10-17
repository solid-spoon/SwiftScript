#ifndef CODEGEN_H
#define CODEGEN_H
#include "ast.h"

/* Returns the generated JS string on success, NULL on codegen error.
   On NULL, use the accessors below to get the diagnostic. */
char *gen_program(Node *prog);

int         gen_had_error(void);
const char *gen_error_msg(void);
int         gen_error_line(void);
int         gen_error_col(void);

#endif