#include "buffer.h"

#include <stdlib.h>
#include <string.h>

void buf_init(buffer_t* b) {
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void buf_append(buffer_t* b, const char* src, size_t n) {

    if (b->len + n > b->cap) {

        size_t required = b->len + n;
        size_t new_cap = (b->cap == 0) ? 16 : b->cap * 2;

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

void buf_free(buffer_t* b) {
    free(b->data);
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}
