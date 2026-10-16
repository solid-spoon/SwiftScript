#include "lexer.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

static const char *KEYWORDS[] = {
    "let","var","func","return","if","else","for","in","while","break","continue",
    "struct","enum","class","protocol","extension","import","export","from","as","true","false","nil",
    "self","init","guard","defer","throws","try","catch","switch","case","default",
    "where","inout","async","await","public","private","internal","static","lazy",
    NULL
};

static int is_keyword(const char *w) {
    for (int i = 0; KEYWORDS[i]; i++)
        if (strcmp(KEYWORDS[i], w) == 0) return 1;
    return 0;
}

static int is_id_start(int c) {
    return isalpha((unsigned char)c) || c == '_' || c == '$';
}
static int is_id_part(int c) {
    return isalnum((unsigned char)c) || c == '_' || c == '$';
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
    TokenList *toks = xcalloc(1, sizeof(TokenList));
    size_t n = strlen(src);
    size_t i = 0;
    int line = 1, col = 1;

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
            if (i < n) ADV(2);
            continue;
        }

        if (isdigit((unsigned char)c)) {
            size_t s = i;
            while (i < n && (isdigit((unsigned char)src[i]) || src[i] == '_')) ADV(1);
            if (i < n && src[i] == '.' && i + 1 < n && isdigit((unsigned char)src[i+1])) {
                ADV(1);
                while (i < n && (isdigit((unsigned char)src[i]) || src[i] == '_')) ADV(1);
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

        if (c == '"' || c == '\'') {
            char q = c;
            ADV(1);
            StrBuf sb; sb_init(&sb);
            while (i < n && src[i] != q) {
                if (src[i] == '\\' && i + 1 < n) {
                    sb_putc(&sb, src[i]);
                    sb_putc(&sb, src[i+1]);
                    ADV(2);
                } else {
                    sb_putc(&sb, src[i]);
                    ADV(1);
                }
            }
            if (i >= n) {
                if (err) { err->line = sl; err->col = sc;
                    snprintf(err->msg, sizeof(err->msg), "Незакрытая строка"); }
                free_tokens(toks);
                return NULL;
            }
            ADV(1);
            tl_push(toks, T_STR, sb_take(&sb), 0, sl, sc);
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

        /* Односимвольные операторы */
        if (strchr("+-*/%<>()[]{},:.=!?@_&|", c)) {
            char buf[2] = { c, 0 };
            tl_push(toks, T_OP, xstrdup(buf), 0, sl, sc);
            ADV(1);
            continue;
        }

        if (err) {
            err->line = sl; err->col = sc;
            snprintf(err->msg, sizeof(err->msg), "Неизвестный символ '%c'", c);
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