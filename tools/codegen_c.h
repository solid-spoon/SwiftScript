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
#ifndef CODEGEN_C_H
#define CODEGEN_C_H

#include "ast.h"

char *gen_c_program(Node *prog);

char *gen_c_get_sourcemap(const char *source_file);

const char *gen_c_error_msg(void);
int         gen_c_error_line(void);
int         gen_c_error_col(void);

#endif
