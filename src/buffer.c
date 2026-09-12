#include "buffer.h"

#include <stdlib.h>
#include <string.h>

void buf_init(buffer* b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void buf_append(buffer* b, const char* src, usize n) {

    if (b->len + n > b->cap) {

        usize required = b->len + n;
        usize new_cap = (b->cap == 0) ? 16 : b->cap * 2;

        if (new_cap < required) {
            new_cap = required;
        }

        char* new_data = realloc(b->data, new_cap);

        if (new_data == NULL) {
            return;
        }

        b->data = new_data;
        b->cap = new_cap;
    }

    memcpy(b->data + b->len, src, n);
    b->len += n;
}

void buf_free(buffer* b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

b8 buf_consume(buffer* b, usize n) {

    if (n > b->len) {
        return false;
    }

    memmove(b->data, b->data + n, b->len - n);
    b->len -= n;
    return true;
}
