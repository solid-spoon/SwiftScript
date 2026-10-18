#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Compile an AST program to JavaScript source.
 *
 * Returns:
 *   - A malloc()-ed, NUL-terminated JS string on success.
 *     Caller MUST free() it (plain free(), not sb_free()).
 *   - NULL on codegen error. Use the accessors below to retrieve
 *     the diagnostic; they return the *first* error recorded.
 *
 * Preconditions:
 *   - prog != NULL and points to a valid N_*_PROGRAM node
 *     (i.e. the return value of parse()). Passing NULL is undefined.
 *   - Single-threaded: gen_program uses internal global state for
 *     temp-label uniqueness and error reporting. Concurrent calls
 *     (or reentrant calls from within the AST) are UB.
 *
 * Postconditions:
 *   - The AST pointed to by prog is NOT modified and NOT freed.
 *     Ownership remains with the caller.
 *
 * Re-entrancy: calling gen_program multiple times sequentially is
 * supported; each call resets the internal counter and error state. */
char *gen_program(Node *prog);

/* Diagnostic accessors. Valid after gen_program() returns NULL. */
int         gen_had_error(void);   /* 1 if an error was recorded     */
const char *gen_error_msg(void);   /* NUL-terminated message         */
int         gen_error_line(void);  /* 1-based, may be 0 if unknown   */
int         gen_error_col(void);   /* 1-based, may be 0 if unknown   */

#ifdef __cplusplus
}
#endif

#endif /* CODEGEN_H */