#ifndef BUFFER_H
#define BUFFER_H

#include <stddef.h>

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} buffer_t;

void buf_init(buffer_t* b);
void buf_append(buffer_t* b, const char* src, size_t n);
void buf_free(buffer_t* b);

#endif
