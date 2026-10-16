#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdarg.h>

void  *xmalloc(size_t n);
void  *xcalloc(size_t n, size_t sz);
void  *xrealloc(void *p, size_t n);
char  *xstrdup(const char *s);
char  *xstrndup(const char *s, size_t n);

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} StrBuf;

void  sb_init(StrBuf *sb);
void  sb_put(StrBuf *sb, const char *s);
void  sb_putn(StrBuf *sb, const char *s, size_t n);
void  sb_putc(StrBuf *sb, char c);
void  sb_printf(StrBuf *sb, const char *fmt, ...);
char *sb_take(StrBuf *sb);
void  sb_free(StrBuf *sb);

typedef struct {
    int  line;
    int  col;
    char msg[512];
} SSError;

#endif