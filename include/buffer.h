#ifndef BUFFER_H
#define BUFFER_H

#include "types.h"

typedef struct {
    char* data;
    usize len;
    usize cap;
    b8 oom;
} buffer;

void buf_init(buffer* b);
b8 buf_append(buffer* b, const char* src, usize n);
b8 buf_reserve(buffer* b, usize n);
void buf_free(buffer* b);
b8 buf_consume(buffer* b, usize n);

#endif
