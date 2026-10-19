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
#include "sws_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

SwsTryFrame *_sws_try_top = NULL;
SwsVal _sws_thrown;

SwsVal sws_undef(void) { SwsVal v; v.t = SWS_UNDEF; v.n = 0; return v; }
SwsVal sws_null(void) { SwsVal v; v.t = SWS_NULL; v.n = 0; return v; }
SwsVal sws_bool(int b) { SwsVal v; v.t = SWS_BOOL; v.b = b; return v; }
SwsVal sws_num(double n) { SwsVal v; v.t = SWS_NUM; v.n = n; return v; }

SwsVal sws_str(const char *s) {
    SwsVal v; v.t = SWS_STR;
    if (!s) v.s = NULL;
    else {
        char *copy = malloc(strlen(s) + 1);
        strcpy(copy, s);
        v.s = copy;
    }
    return v;
}

SwsVal sws_arr(int n, SwsVal *items) {
    SwsArr *a = malloc(sizeof(SwsArr));
    a->len = a->cap = n;
    a->items = malloc(sizeof(SwsVal) * (n > 0 ? n : 1));
    for (int i=0; i<n; i++) a->items[i] = items[i];
    SwsVal v; v.t = SWS_ARR; v.arr = a; return v;
}

SwsVal sws_obj_new(void) {
    SwsObj *o = malloc(sizeof(SwsObj));
    o->len = 0; o->cap = 8;
    o->keys = malloc(sizeof(char*) * o->cap);
    o->vals = malloc(sizeof(SwsVal) * o->cap);
    SwsVal v; v.t = SWS_OBJ; v.obj = o; return v;
}

void sws_obj_set(SwsVal o, const char *k, SwsVal v) {
    if (o.t != SWS_OBJ) return;
    SwsObj *obj = o.obj;
    for (int i=0; i<obj->len; i++) {
        if (strcmp(obj->keys[i], k) == 0) {
            obj->vals[i] = v;
            return;
        }
    }
    if (obj->len == obj->cap) {
        obj->cap *= 2;
        obj->keys = realloc(obj->keys, sizeof(char*) * obj->cap);
        obj->vals = realloc(obj->vals, sizeof(SwsVal) * obj->cap);
    }
    obj->keys[obj->len] = strdup(k);
    obj->vals[obj->len] = v;
    obj->len++;
}

SwsVal sws_obj_get(SwsVal o, const char *k) {
    if (o.t != SWS_OBJ) return sws_undef();
    SwsObj *obj = o.obj;
    for (int i=0; i<obj->len; i++)
        if (strcmp(obj->keys[i], k) == 0) return obj->vals[i];
    return sws_undef();
}

SwsVal sws_opt_get(SwsVal o, const char *k) {
    if (sws_is_nullish(o)) return sws_null();
    return sws_obj_get(o, k);
}

SwsVal sws_obj_from(int n, const char *const *keys, SwsVal *vals) {
    SwsVal o = sws_obj_new();
    for (int i=0; i<n; i++) sws_obj_set(o, keys[i], vals[i]);
    return o;
}

SwsVal sws_obj_case(const char *tag, int n, SwsVal *vals) {
    SwsVal o = sws_obj_new();
    sws_obj_set(o, "_case", sws_str(tag));
    for (int i=0; i<n; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "_%d", i);
        sws_obj_set(o, buf, vals[i]);
    }
    return o;
}

int sws_is_null(SwsVal v) { return v.t == SWS_NULL; }
int sws_is_nullish(SwsVal v) { return v.t == SWS_NULL || v.t == SWS_UNDEF; }
int sws_truthy(SwsVal v) {
    switch (v.t) {
        case SWS_BOOL: return v.b;
        case SWS_NUM: return v.n != 0 && !isnan(v.n);
        case SWS_STR: return v.s && v.s[0] != '\0';
        case SWS_NULL:
        case SWS_UNDEF: return 0;
        default: return 1;
    }
}

int sws_str_eq(SwsVal v, const char *s) {
    if (v.t != SWS_STR) return 0;
    if (!v.s && !s) return 1;
    if (!v.s || !s) return 0;
    return strcmp(v.s, s) == 0;
}
int sws_num_eq(SwsVal v, double n) { return v.t == SWS_NUM && v.n == n; }
int sws_case_is(SwsVal v, const char *tag) {
    if (v.t != SWS_OBJ) return 0;
    SwsVal c = sws_obj_get(v, "_case");
    return sws_str_eq(c, tag);
}

SwsVal sws_index(SwsVal o, SwsVal idx) {
    if (o.t == SWS_ARR) {
        int i = (int)idx.n;
        if (i >= 0 && i < (int)o.arr->len) return o.arr->items[i];
        return sws_undef();
    }
    if (o.t == SWS_OBJ && idx.t == SWS_STR) return sws_obj_get(o, idx.s);
    return sws_undef();
}

SwsVal sws_add(SwsVal a, SwsVal b) {
    if (a.t == SWS_STR || b.t == SWS_STR) {
        SwsVal sa = sws_to_string(a);
        SwsVal sb = sws_to_string(b);
        char *res = malloc(strlen(sa.s) + strlen(sb.s) + 1);
        strcpy(res, sa.s); strcat(res, sb.s);
        SwsVal v = sws_str(res); free(res); return v;
    }
    return sws_num(a.n + b.n);
}
SwsVal sws_sub(SwsVal a, SwsVal b) { return sws_num(a.n - b.n); }
SwsVal sws_mul(SwsVal a, SwsVal b) { return sws_num(a.n * b.n); }
SwsVal sws_div(SwsVal a, SwsVal b) { return sws_num(a.n / b.n); }
SwsVal sws_mod(SwsVal a, SwsVal b) { return sws_num((long long)a.n % (long long)b.n); }
SwsVal sws_neg(SwsVal v) { return sws_num(-v.n); }

SwsVal sws_eq(SwsVal a, SwsVal b) {
    if (a.t == b.t) {
        switch (a.t) {
            case SWS_BOOL: return sws_bool(a.b == b.b);
            case SWS_NUM: return sws_bool(a.n == b.n);
            case SWS_STR: return sws_bool(sws_str_eq(a, b.s));
            case SWS_NULL:
            case SWS_UNDEF: return sws_bool(1);
            default: return sws_bool(a.obj == b.obj);
        }
    }
    return sws_bool(0);
}

SwsVal sws_lt(SwsVal a, SwsVal b) { return sws_bool(a.n < b.n); }
SwsVal sws_gt(SwsVal a, SwsVal b) { return sws_bool(a.n > b.n); }
SwsVal sws_le(SwsVal a, SwsVal b) { return sws_bool(a.n <= b.n); }
SwsVal sws_ge(SwsVal a, SwsVal b) { return sws_bool(a.n >= b.n); }

SwsVal sws_concat(int n, SwsVal *parts) {
    size_t total = 0;
    SwsVal *strs = malloc(sizeof(SwsVal) * (n > 0 ? n : 1));
    for (int i=0; i<n; i++) {
        strs[i] = sws_to_string(parts[i]);
        total += strlen(strs[i].s);
    }
    char *res = malloc(total + 1); res[0] = '\0';
    for (int i=0; i<n; i++) strcat(res, strs[i].s);
    free(strs);
    SwsVal v = sws_str(res); free(res); return v;
}

SwsVal sws_to_string(SwsVal v) {
    char buf[64];
    switch (v.t) {
        case SWS_UNDEF: return sws_str("undefined");
        case SWS_NULL: return sws_str("nil");
        case SWS_BOOL: return sws_str(v.b ? "true" : "false");
        case SWS_NUM:
            if (v.n == (long long)v.n) {
                snprintf(buf, sizeof(buf), "%lld", (long long)v.n);
            } else {
                /* Shortest round-trip: find the smallest precision that
                 * parses back to the exact same double. Matches JS. */
                for (int p = 1; p <= 17; p++) {
                    snprintf(buf, sizeof(buf), "%.*g", p, v.n);
                    if (strtod(buf, NULL) == v.n) break;
                }
            }
            return sws_str(buf);
        case SWS_STR: return v;
        default: return sws_str("[object]");
    }
}

SwsVal sws_to_int(SwsVal v) { return sws_num((long long)v.n); }
SwsVal sws_to_double(SwsVal v) { return sws_num(v.n); }
SwsVal sws_to_bool(SwsVal v) { return sws_bool(sws_truthy(v)); }

SwsVal sws_abs(SwsVal v) { return sws_num(fabs(v.n)); }
SwsVal sws_min(SwsVal a, SwsVal b) { return sws_num(a.n < b.n ? a.n : b.n); }
SwsVal sws_max(SwsVal a, SwsVal b) { return sws_num(a.n > b.n ? a.n : b.n); }
SwsVal sws_round(SwsVal v) { return sws_num(round(v.n)); }
SwsVal sws_floor(SwsVal v) { return sws_num(floor(v.n)); }
SwsVal sws_ceil(SwsVal v) { return sws_num(ceil(v.n)); }

SwsVal sws_force(SwsVal v) {
    if (sws_is_nullish(v)) { fprintf(stderr, "Force unwrap of nil\n"); exit(1); }
    return v;
}

SwsVal sws_range(double a, double b) {
    int len = (int)(b - a); if (len < 0) len = 0;
    SwsVal *items = malloc(sizeof(SwsVal) * (len > 0 ? len : 1));
    for (int i=0; i<len; i++) items[i] = sws_num(a + i);
    SwsVal v = sws_arr(len, items); free(items); return v;
}

SwsVal sws_range_in(double a, double b) {
    int len = (int)(b - a + 1); if (len < 0) len = 0;
    SwsVal *items = malloc(sizeof(SwsVal) * (len > 0 ? len : 1));
    for (int i=0; i<len; i++) items[i] = sws_num(a + i);
    SwsVal v = sws_arr(len, items); free(items); return v;
}

SwsVal sws_as_array(SwsVal v) {
    if (v.t == SWS_ARR) return v;
    return sws_arr(0, NULL);
}

SwsVal sws_println(int n, SwsVal *vals) {
    for (int i=0; i<n; i++) {
        if (i) printf(" ");
        SwsVal s = sws_to_string(vals[i]);
        printf("%s", s.s ? s.s : "nil");
    }
    printf("\n");
    return sws_undef();
}

SwsVal sws_method(SwsVal self, SwsMethodPtr fn) {
    SwsVal v; v.t = SWS_FUNC;
    v.func.is_method = 1;
    v.func.method = fn;
    v.func.env = malloc(sizeof(SwsVal));
    *(SwsVal*)v.func.env = self;
    v.func.props = NULL;
    return v;
}

SwsVal sws_call(SwsVal fn, int argc, SwsVal *argv) {
    if (fn.t == SWS_FUNC) {
        if (fn.func.is_method)
            return fn.func.method(*(SwsVal*)fn.func.env, argc, argv);
        else if (fn.func.fn)
            return fn.func.fn(argc, argv);
    }
    return sws_undef();
}

void sws_throw(SwsVal v) {
    _sws_thrown = v;
    if (_sws_try_top) longjmp(_sws_try_top->buf, 1);
    else { fprintf(stderr, "Uncaught exception\n"); exit(1); }
}

void sws_rethrow(void) { sws_throw(_sws_thrown); }
SwsVal sws_try_opt(SwsVal v) { return v; }
SwsVal sws_try_force(SwsVal v) { return v; }
