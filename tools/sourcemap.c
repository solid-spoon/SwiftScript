#include "sourcemap.h"
#include "util.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static const char BASE64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void vlq_encode(int value, StrBuf *sb) {
    int vlq = value < 0 ? ((-value) << 1) | 1 : (value << 1);
    do {
        int digit = vlq & 31;
        vlq >>= 5;
        if (vlq > 0) digit |= 32;
        sb_putc(sb, BASE64[digit]);
    } while (vlq > 0);
}

void sm_init(SourceMap *sm) {
    sm->orig_lines = NULL;
    sm->orig_cols = NULL;
    sm->gen_lines = NULL;
    sm->count = 0;
    sm->cap = 0;
}

void sm_add(SourceMap *sm, int gen_line, int orig_line, int orig_col) {
    if (sm->count == sm->cap) {
        sm->cap = sm->cap ? sm->cap * 2 : 128;
        sm->orig_lines = xrealloc(sm->orig_lines, sizeof(int) * sm->cap);
        sm->orig_cols = xrealloc(sm->orig_cols, sizeof(int) * sm->cap);
        sm->gen_lines = xrealloc(sm->gen_lines, sizeof(int) * sm->cap);
    }
    sm->gen_lines[sm->count] = gen_line;
    sm->orig_lines[sm->count] = orig_line;
    sm->orig_cols[sm->count] = orig_col;
    sm->count++;
}

char *sm_generate_json(SourceMap *sm, const char *source_file) {
    StrBuf sb;
    sb_init(&sb);

    sb_printf(&sb, "{\"version\":3,\"sources\":[\"%s\"],\"names\":[],\"mappings\":\"", source_file);

    int prev_gen_line = 0;
    int prev_orig_line = 0;
    int prev_orig_col = 0;
    int prev_gen_col = 0;

    for (int i = 0; i < sm->count; i++) {
        int gen_line = sm->gen_lines[i];
        int orig_line = sm->orig_lines[i];
        int orig_col = sm->orig_cols[i];

        while (prev_gen_line < gen_line) {
            sb_putc(&sb, ';');
            prev_gen_line++;
            prev_gen_col = 0;
        }

        if (i > 0 && gen_line == sm->gen_lines[i - 1]) {
            sb_putc(&sb, ',');
        }

        // VLQ: [gen_col_delta, source_idx_delta, orig_line_delta, orig_col_delta]
        vlq_encode(0 - prev_gen_col, &sb); 
        prev_gen_col = 0;

        vlq_encode(0, &sb); // source index

        vlq_encode((orig_line - 1) - prev_orig_line, &sb);
        prev_orig_line = orig_line - 1;

        vlq_encode(orig_col - 1 - prev_orig_col, &sb);
        prev_orig_col = orig_col - 1;
    }

    sb_put(&sb, "\"}");
    return sb_take(&sb);
}

void sm_free(SourceMap *sm) {
    free(sm->orig_lines);
    free(sm->orig_cols);
    free(sm->gen_lines);
    sm->count = sm->cap = 0;
}