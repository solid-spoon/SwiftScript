#ifndef SOURCEMAP_H
#define SOURCEMAP_H

#include <stddef.h>

typedef struct {
    int *orig_lines;
    int *orig_cols;
    int *gen_lines;
    int count;
    int cap;
} SourceMap;

void sm_init(SourceMap *sm);
void sm_add(SourceMap *sm, int gen_line, int orig_line, int orig_col);
char *sm_generate_json(SourceMap *sm, const char *source_file);
void sm_free(SourceMap *sm);

#endif
