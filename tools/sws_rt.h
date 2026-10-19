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
#ifndef SWS_RT_H
#define SWS_RT_H
#include <stddef.h>
#include <setjmp.h>

typedef struct SwsVal   SwsVal;
typedef struct SwsArr   SwsArr;
typedef struct SwsObj   SwsObj;
typedef struct SwsFunc  SwsFunc;

typedef enum {
    SWS_UNDEF, SWS_NULL, SWS_BOOL, SWS_NUM, SWS_STR,
    SWS_ARR, SWS_OBJ, SWS_FUNC, SWS_RANGE
} SwsType;

typedef SwsVal (*SwsFnPtr)(int argc, SwsVal *argv);
typedef SwsVal (*SwsMethodPtr)(SwsVal self, int argc, SwsVal *argv);

struct SwsVal {
    SwsType t;
    union {
        int         b;
        double      n;
        const char *s;
        SwsArr     *arr;
        SwsObj     *obj;
        struct { 
            SwsFnPtr fn; 
            SwsMethodPtr method;
            int is_method;
            void *env; 
            SwsObj *props; 
        } func;
        struct { double lo, hi; int incl; } range;
    };
};

struct SwsArr { size_t len, cap; SwsVal *items; };
struct SwsObj { size_t len, cap; char **keys; SwsVal *vals; };

/* constructors */
SwsVal sws_undef(void);
SwsVal sws_null(void);
SwsVal sws_bool(int b);
SwsVal sws_num(double n);
SwsVal sws_str(const char *s);
SwsVal sws_arr(int n, SwsVal *items);
SwsVal sws_obj_new(void);
SwsVal sws_obj_from(int n, const char *const *keys, SwsVal *vals);
SwsVal sws_obj_case(const char *tag, int n, SwsVal *vals);

/* property access / assignment */
SwsVal sws_obj_get(SwsVal o, const char *k);
SwsVal sws_opt_get(SwsVal o, const char *k);
void   sws_obj_set(SwsVal o, const char *k, SwsVal v);
SwsVal sws_index(SwsVal o, SwsVal idx);

/* operators */
SwsVal sws_add(SwsVal, SwsVal);
SwsVal sws_sub(SwsVal, SwsVal);
SwsVal sws_mul(SwsVal, SwsVal);
SwsVal sws_div(SwsVal, SwsVal);
SwsVal sws_mod(SwsVal, SwsVal);
SwsVal sws_neg(SwsVal);
SwsVal sws_eq(SwsVal, SwsVal);
SwsVal sws_lt(SwsVal, SwsVal);
SwsVal sws_gt(SwsVal, SwsVal);
SwsVal sws_le(SwsVal, SwsVal);
SwsVal sws_ge(SwsVal, SwsVal);
int    sws_str_eq(SwsVal, const char *);
int    sws_num_eq(SwsVal, double);
int    sws_case_is(SwsVal, const char *);
int    sws_is_null(SwsVal);
int    sws_is_nullish(SwsVal);
int    sws_truthy(SwsVal);

/* collections */
SwsVal sws_range(double a, double b);
SwsVal sws_range_in(double a, double b);
SwsVal sws_as_array(SwsVal v);
SwsVal sws_concat(int n, SwsVal *parts);

/* function values */
SwsVal sws_call(SwsVal fn, int argc, SwsVal *argv);
SwsVal sws_method(SwsVal self, SwsMethodPtr fn);

/* builtins */
SwsVal sws_println(int n, SwsVal *vals);
SwsVal sws_to_string(SwsVal v);
SwsVal sws_to_int(SwsVal v);
SwsVal sws_to_double(SwsVal v);
SwsVal sws_to_bool(SwsVal v);
SwsVal sws_abs(SwsVal v);
SwsVal sws_min(SwsVal a, SwsVal b);
SwsVal sws_max(SwsVal a, SwsVal b);
SwsVal sws_round(SwsVal v);
SwsVal sws_floor(SwsVal v);
SwsVal sws_ceil(SwsVal v);
SwsVal sws_force(SwsVal v);

/* try/catch via setjmp */
typedef struct SwsTryFrame { jmp_buf buf; struct SwsTryFrame *prev; } SwsTryFrame;
extern SwsTryFrame *_sws_try_top;
extern SwsVal        _sws_thrown;
void   sws_throw(SwsVal v);
void   sws_rethrow(void);
SwsVal sws_try_opt(SwsVal v);
SwsVal sws_try_force(SwsVal v);

#endif
