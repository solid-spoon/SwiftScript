#include "util.h"

#include <stdarg.h>   /* FIX #3: va_list / va_start / va_end / va_copy */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- allocation ---------- */

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) {
        fprintf(stderr, "swsc: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

void *xcalloc(size_t n, size_t sz) {
    if (n != 0 && sz > SIZE_MAX / n) {
        fprintf(stderr, "swsc: allocation size overflow (%zu * %zu)\n", n, sz);
        exit(EXIT_FAILURE);
    }
    void *p = calloc(n ? n : 1, sz ? sz : 1);
    if (!p) {
        fprintf(stderr, "swsc: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return p;
}

void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) {
        fprintf(stderr, "swsc: out of memory\n");
        exit(EXIT_FAILURE);
    }
    return q;
}

char *xstrdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s);
    char *p = xmalloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

/* FIX #1: original read n bytes unconditionally, overrunning the source
 * when strlen(s) < n. Now stops at the first NUL, like strndup. */
char *xstrndup(const char *s, size_t n) {
    if (!s) return NULL;
    size_t len = 0;
    while (len < n && s[len] != '\0') len++;
    char *p = xmalloc(len + 1);
    if (len) memcpy(p, s, len);
    p[len] = '\0';
    return p;
}

/* ---------- StrBuf ---------- */

static void sb_reserve(StrBuf *sb, size_t need) {
    if (need >= SIZE_MAX - sb->len) {
        fprintf(stderr, "swsc: string too large\n");
        exit(EXIT_FAILURE);
    }
    size_t want = sb->len + need + 1;
    if (want <= sb->cap) return;

    size_t cap = sb->cap ? sb->cap : 64;
    while (cap < want) {
        if (cap > SIZE_MAX / 2) { cap = want; break; }
        cap *= 2;
    }
    sb->buf = xrealloc(sb->buf, cap);
    sb->cap = cap;
}

void sb_init(StrBuf *sb) { sb->buf = NULL; sb->len = 0; sb->cap = 0; }

void sb_put(StrBuf *sb, const char *s) {
    sb_putn(sb, s, s ? strlen(s) : 0);
}

void sb_putn(StrBuf *sb, const char *s, size_t n) {
    /* FIX #4: never dereference NULL; keep buffer valid & terminated. */
    if (s == NULL || n == 0) {
        sb_reserve(sb, 0);
        sb->buf[sb->len] = '\0';
        return;
    }

    /* FIX #2: if s points into sb->buf, remember its offset so we can
     * re-derive the pointer after sb_reserve (realloc may move the
     * buffer, invalidating s). */
    size_t off = 0;
    int aliased = 0;
    if (sb->buf != NULL && sb->cap > 0) {
        uintptr_t s_addr = (uintptr_t)s;
        uintptr_t b_addr = (uintptr_t)sb->buf;
        if (s_addr >= b_addr && s_addr - b_addr < sb->cap) {
            off     = (size_t)(s_addr - b_addr);
            aliased = 1;
        }
    }

    sb_reserve(sb, n);

    if (aliased) {
        s = sb->buf + off;
        /* Source and destination live in the same buffer → overlap
         * is possible (e.g. sb_put(sb, sb->buf) with a prefix already
         * present). memmove is required. */
        memmove(sb->buf + sb->len, s, n);
    } else {
        memcpy(sb->buf + sb->len, s, n);
    }
    sb->len += n;
    sb->buf[sb->len] = '\0';
}

void sb_putc(StrBuf *sb, char c) {
    sb_reserve(sb, 1);
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
}

void sb_printf(StrBuf *sb, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (n < 0) {
        va_end(ap2);
        fprintf(stderr, "swsc: vsnprintf failed\n");
        exit(EXIT_FAILURE);
    }
    sb_reserve(sb, (size_t)n);
    vsnprintf(sb->buf + sb->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    sb->len += (size_t)n;
}

/* FIX #8: don't allocate 64 bytes just to return an empty string. */
char *sb_take(StrBuf *sb) {
    if (!sb->buf) {
        char *out = xmalloc(1);
        out[0] = '\0';
        return out;
    }
    char *out = sb->buf;
    sb->buf = NULL;
    sb->len = 0;
    sb->cap = 0;
    return out;
}

void sb_free(StrBuf *sb) {
    free(sb->buf);
    sb->buf = NULL;
    sb->len = sb->cap = 0;
}
