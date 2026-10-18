#include "lexer.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>

static const char *KEYWORDS[] = {
    "as", "async", "await", "break", "case", "catch", "class", "continue", 
    "default", "defer", "else", "enum", "export", "extends", "extension", 
    "false", "for", "from", "func", "guard", "if", "implements", "import", 
    "init", "inout", "in", "internal", "lazy", "let", "new", "nil", "override", 
    "private", "protocol", "public", "return", "self", "static", "struct", 
    "super", "switch", "throws", "throw", "true", "try", "var", "where", "while", 
    NULL
};

static int is_keyword(const char *w) {
    for (int i = 0; KEYWORDS[i]; i++)
        if (strcmp(KEYWORDS[i], w) == 0) return 1;
    return 0;
}

static int is_id_start(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$';
}
static int is_id_part(int c) {
    return is_id_start(c) || (c >= '0' && c <= '9');
}

static void tl_push(TokenList *t, TokType type, char *value, double num, int line, int col) {
    if (t->count == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 64;
        t->items = xrealloc(t->items, sizeof(Token) * t->cap);
    }
    Token *tk = &t->items[t->count++];
    tk->type = type;
    tk->value = value;
    tk->num   = num;
    tk->line  = line;
    tk->col   = col;
}

TokenList *tokenize(const char *src, SSError *err) {
    if (!src) {
        if (err) { err->line = 1; err->col = 1;
            snprintf(err->msg, sizeof(err->msg), "Empty input"); }
        return NULL;
    }
    TokenList *toks = xcalloc(1, sizeof(TokenList));
    size_t n = strlen(src);
    size_t i = 0;
    int line = 1, col = 1;

    /* Skip UTF-8 BOM if present (common on Windows-authored files). */
    if (n >= 3 &&
        (unsigned char)src[0] == 0xEF &&
        (unsigned char)src[1] == 0xBB &&
        (unsigned char)src[2] == 0xBF) {
        i = 3;
    }

    #define ADV(k) do { \
        for (int _k = 0; _k < (k); _k++) { \
            if (i < n && src[i] == '\n') { line++; col = 1; } else col++; \
            i++; \
        } \
    } while (0)

    while (i < n) {
        char c = src[i];
        int sl = line, sc = col;

        if (isspace((unsigned char)c)) { ADV(1); continue; }
        if (c == '/' && i + 1 < n && src[i+1] == '/') {
            while (i < n && src[i] != '\n') ADV(1);
            continue;
        }
        if (c == '/' && i + 1 < n && src[i+1] == '*') {
            ADV(2);
            while (i < n && !(src[i] == '*' && i + 1 < n && src[i+1] == '/')) ADV(1);
            if (i >= n) {
                if (err) { err->line = sl; err->col = sc;
                    snprintf(err->msg, sizeof(err->msg),
                             "Unterminated block comment"); }
                free_tokens(toks);
                return NULL;
            }
            ADV(2);
            continue;
        }

        if (isdigit((unsigned char)c)) {
            size_t s = i;
            int base = 10;
            const char *basename = NULL;

            /* 0x / 0b / 0o */
            if (c == '0' && i + 1 < n) {
                char c1 = src[i+1];
                if (c1 == 'x' || c1 == 'X') { base = 16; basename = "hex"; }
                else if (c1 == 'b' || c1 == 'B') { base = 2;  basename = "binary"; }
                else if (c1 == 'o' || c1 == 'O') { base = 8;  basename = "octal"; }
                if (base != 10) ADV(2);
            }

            if (base != 10) {
                size_t digits_start = i;
                int digit_count = 0;
                int prev_underscore = 0;
                while (i < n) {
                    char ch = src[i];
                    if (ch == '_') {
                        if (digit_count == 0 || prev_underscore) break;
                        prev_underscore = 1;
                        ADV(1);
                        continue;
                    }
                    int is_digit = 0;
                    if (base == 16) is_digit = isxdigit((unsigned char)ch);
                    else if (base == 2) is_digit = (ch == '0' || ch == '1');
                    else if (base == 8) is_digit = (ch >= '0' && ch <= '7');
                    if (!is_digit) break;
                    prev_underscore = 0;
                    digit_count++;
                    ADV(1);
                }
                if (digit_count == 0 || prev_underscore) {
                    if (err) { err->line = sl; err->col = sc;
                        snprintf(err->msg, sizeof(err->msg),
                                 "Invalid %s literal", basename); }
                    free_tokens(toks);
                    return NULL;
                }
                char *raw = xstrndup(src + digits_start, i - digits_start);
                size_t k = 0;
                for (size_t j = 0; raw[j]; j++) if (raw[j] != '_') raw[k++] = raw[j];
                raw[k] = '\0';
                errno = 0;
                unsigned long long uv = strtoull(raw, NULL, base);
                double val = (errno == ERANGE) ? strtod(raw, NULL) : (double)uv;
                free(raw);
                tl_push(toks, T_NUM, NULL, val, sl, sc);
                continue;
            }

            /* [digits] [. digits] [ (e|E) [+|-] digits ] */
            int prev_underscore = 0;
            int digit_count = 0;
            while (i < n) {
                char ch = src[i];
                if (ch == '_') {
                    if (digit_count == 0 || prev_underscore) break;
                    prev_underscore = 1;
                    ADV(1);
                    continue;
                }
                if (!isdigit((unsigned char)ch)) break;
                prev_underscore = 0;
                digit_count++;
                ADV(1);
            }
            if (prev_underscore) {
                i--; col--;
            }

            if (i < n && src[i] == '.' && i + 1 < n &&
                isdigit((unsigned char)src[i+1])) {
                ADV(1);
                prev_underscore = 0;
                while (i < n) {
                    char ch = src[i];
                    if (ch == '_') {
                        if (prev_underscore) break;
                        prev_underscore = 1;
                        ADV(1);
                        continue;
                    }
                    if (!isdigit((unsigned char)ch)) break;
                    prev_underscore = 0;
                    ADV(1);
                }
            }

            if (i < n && (src[i] == 'e' || src[i] == 'E')) {
                size_t save = i;
                int save_line = line, save_col = col;
                ADV(1);
                if (i < n && (src[i] == '+' || src[i] == '-')) ADV(1);
                if (i < n && isdigit((unsigned char)src[i])) {
                    while (i < n && isdigit((unsigned char)src[i])) ADV(1);
                } else {
                    i = save;
                    line = save_line;
                    col = save_col;
                }
            }

            char *raw = xstrndup(src + s, i - s);
            size_t k = 0;
            for (size_t j = 0; raw[j]; j++) if (raw[j] != '_') raw[k++] = raw[j];
            raw[k] = '\0';
            double val = strtod(raw, NULL);
            free(raw);
            tl_push(toks, T_NUM, NULL, val, sl, sc);
            continue;
        }

                if (c == '"') {
            int is_multiline = 0;
            if (i + 2 < n && src[i+1] == '"' && src[i+2] == '"') {
                is_multiline = 1;
                ADV(3);
            } else {
                ADV(1);
            }

            StrBuf sb; sb_init(&sb);
            int terminated = 0;

            while (i < n) {
                if (is_multiline && src[i] == '"' && i + 2 < n && src[i+1] == '"' && src[i+2] == '"') {
                    ADV(3);
                    terminated = 1;
                    break;
                }
                if (!is_multiline && src[i] == '"') {
                    ADV(1);
                    terminated = 1;
                    break;
                }

                if (src[i] == '\\' && i + 1 < n) {
                    sb_putc(&sb, src[i]);
                    sb_putc(&sb, src[i+1]);
                    ADV(2);
                } else {
                    sb_putc(&sb, src[i]);
                    ADV(1);
                }
            }

            if (!terminated) {
                if (err) { err->line = sl; err->col = sc;
                    snprintf(err->msg, sizeof(err->msg), "Unterminated string"); }
                sb_free(&sb);
                free_tokens(toks);
                return NULL;
            }

            char *raw_str = sb_take(&sb);

            if (is_multiline) {
                char *start = raw_str;
                if (*start == '\n') start++;
                else if (*start == '\r' && start[1] == '\n') start += 2;
                else if (*start == '\r') start++;

                size_t len = strlen(start);
                while (len > 0 && (start[len-1] == ' ' || start[len-1] == '\t' || start[len-1] == '\r')) len--;
                if (len > 0 && start[len-1] == '\n') {
                    len--;
                    while (len > 0 && (start[len-1] == ' ' || start[len-1] == '\t' || start[len-1] == '\r')) len--;
                }
                start[len] = '\0';

                int min_indent = 9999;
                int current_indent = 0;
                int in_indent = 1;
                int has_content = 0;
                for (size_t j = 0; j < len; j++) {
                    if (start[j] == '\n') {
                        if (has_content && current_indent < min_indent) min_indent = current_indent;
                        in_indent = 1;
                        current_indent = 0;
                        has_content = 0;
                    } else if (in_indent && (start[j] == ' ' || start[j] == '\t')) {
                        current_indent++;
                    } else {
                        in_indent = 0;
                        has_content = 1;
                    }
                }
                if (has_content && current_indent < min_indent) min_indent = current_indent;
                if (min_indent == 9999) min_indent = 0;

                StrBuf dedented; sb_init(&dedented);
                in_indent = 1;
                int to_skip = 0;
                for (size_t j = 0; j < len; j++) {
                    if (start[j] == '\n') {
                        sb_putc(&dedented, '\n');
                        in_indent = 1;
                        to_skip = 0;
                    } else if (in_indent && to_skip < min_indent && (start[j] == ' ' || start[j] == '\t')) {
                        to_skip++;
                    } else {
                        in_indent = 0;
                        sb_putc(&dedented, start[j]);
                    }
                }
                free(raw_str);
                tl_push(toks, T_STR, sb_take(&dedented), 0, sl, sc);
            } else {
                tl_push(toks, T_STR, raw_str, 0, sl, sc);
            }
            continue;
        }

        if (is_id_start((unsigned char)c)) {
            size_t s = i;
            while (i < n && is_id_part((unsigned char)src[i])) ADV(1);
            char *w = xstrndup(src + s, i - s);
            tl_push(toks, is_keyword(w) ? T_KW : T_ID, w, 0, sl, sc);
            continue;
        }

        if (i + 2 < n) {
            char t3[4] = { src[i], src[i+1], src[i+2], 0 };
            if (strcmp(t3, "...") == 0 || strcmp(t3, "..<") == 0) {
                tl_push(toks, T_OP, xstrdup(t3), 0, sl, sc);
                ADV(3);
                continue;
            }
        }

        if (i + 1 < n) {
            char t2[3] = { src[i], src[i+1], 0 };
            static const char *OPS2[] = {
                "->","==","!=","<=",">=","&&","||","??","?.","+=","-=","*=","/=","%=", NULL
            };
            int found = 0;
            for (int k = 0; OPS2[k]; k++)
                if (strcmp(t2, OPS2[k]) == 0) { found = 1; break; }
            if (found) {
                tl_push(toks, T_OP, xstrdup(t2), 0, sl, sc);
                ADV(2);
                continue;
            }
        }

        if (strchr("+-*/%<>()[]{},;:.=!?@_&|~^", (unsigned char)c)) {
            char buf[2] = { c, 0 };
            tl_push(toks, T_OP, xstrdup(buf), 0, sl, sc);
            ADV(1);
            continue;
        }

        if (err) {
            err->line = sl; err->col = sc;
            snprintf(err->msg, sizeof(err->msg), "Unknown character '%c'", c);
        }
        free_tokens(toks);
        return NULL;
    }

    tl_push(toks, T_EOF, NULL, 0, line, col);
    #undef ADV
    return toks;
}

void free_tokens(TokenList *t) {
    if (!t) return;
    for (int i = 0; i < t->count; i++) free(t->items[i].value);
    free(t->items);
    free(t);
}