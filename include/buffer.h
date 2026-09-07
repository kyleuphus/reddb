#ifndef BUFFER_H
#define BUFFER_H

#include "types.h"

typedef struct {
    char* data;
    usize len;
    usize cap;
} buffer_t;

void buf_init(buffer_t* b);
void buf_append(buffer_t* b, const char* src, usize n);
void buf_free(buffer_t* b);
b8 buf_consume(buffer_t* b, usize n);

#endif
