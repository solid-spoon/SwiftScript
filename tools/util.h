#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SS_ERR_MSG_CAP 512

/* ---------- allocation ----------
 *
 * All x* helpers exit(EXIT_FAILURE) on allocation failure
 * (after printing a diagnostic to stderr). */

void  *xmalloc(size_t n);
void  *xcalloc(size_t n, size_t sz);
void  *xrealloc(void *p, size_t n);

/* Returns a heap-allocated copy of s, or NULL if s is NULL.
 * xstrndup copies at most n bytes, stopping early at the first NUL —
 * matching strndup semantics (does NOT read past strlen(s)). */
char  *xstrdup(const char *s);
char  *xstrndup(const char *s, size_t n);

/* ---------- StrBuf ----------
 *
 * Growable NUL-terminated string.
 * Invariants:
 *   - buf == NULL  <=>  cap == 0
 *   - if buf != NULL: buf[len] == '\0' and len < cap */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} StrBuf;

/* Must be called exactly once on a fresh StrBuf.
 * Calling it again on a live (non-empty) buffer leaks. */
void  sb_init(StrBuf *sb);

/* Append. If s is NULL (or n == 0 in sb_putn), does nothing beyond
 * ensuring the internal buffer exists and is NUL-terminated.
 * s may alias sb->buf — the aliasing case is handled correctly. */
void  sb_put(StrBuf *sb, const char *s);
void  sb_putn(StrBuf *sb, const char *s, size_t n);
void  sb_putc(StrBuf *sb, char c);

/* Append a formatted string.
 * Precondition: fmt must not point into sb->buf, and no %s/%p argument
 * may point into sb->buf (realloc may move it before the arguments are
 * consumed). */
#if defined(__GNUC__) || defined(__clang__)
void  sb_printf(StrBuf *sb, const char *fmt, ...)
        __attribute__((format(printf, 2, 3)));
#else
void  sb_printf(StrBuf *sb, const char *fmt, ...);
#endif

/* Transfer ownership of the buffer to the caller.
 * The returned pointer is a normal malloc-ed NUL-terminated string and
 * MUST be freed with free() (not sb_free). sb is reset to empty. */
char *sb_take(StrBuf *sb);

/* Release the internal buffer and reset sb to empty. */
void  sb_free(StrBuf *sb);

/* Reset length to 0, keeping the allocated capacity. */
static inline void sb_clear(StrBuf *sb) {
    sb->len = 0;
    if (sb->buf) sb->buf[0] = '\0';
}

/* Filled in by the compiler on a parse error; msg is NUL-terminated. */
typedef struct {
    int  line;
    int  col;
    char msg[SS_ERR_MSG_CAP];
} SSError;

#ifdef __cplusplus
}
#endif

#endif /* UTIL_H */
