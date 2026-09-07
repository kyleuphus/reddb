#include "parser.h"

#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

void parseResult_init(ParseResult* p) {
    p->status = INCOMPLETE;
    p->argc = 0;
    for (int i = 0; i < MAX_ARGS; i++) {
        p->argv[i] = NULL;
    }
    p->bytes_consumed = 0;
}

void parseResult_free(ParseResult* p) {
    for (int i = 0; i < p->argc; i++) {
        free(p->argv[i]);
        p->argv[i] = NULL;
    }
    p->argc = 0;
}

int findTerminator(buffer_t* b, size_t offset) {

    for (size_t i = offset; i + 1 < b->len; i++) {

        if (b->data[i] == '\r' && b->data[i + 1] == '\n') {
            return i;
        }
    }

    return -1;
}

bool readBulk(buffer_t* b, ParseResult* out, size_t offset, size_t* consumed) {

    bool status = false;

    if (offset >= b->len) {
        out->status = INCOMPLETE;
        return status;
    } else if (b->data[offset] != '$') {
        out->status = INVALID;
        return status;
    } else {
        int terminator = findTerminator(b, offset);
        if (terminator == -1) {
            out->status = INCOMPLETE;
            return status;
        } else {
            char tmp[16];
            size_t slice_len = terminator - offset - 1;

            if (slice_len >= sizeof(tmp)) {
                out->status = INVALID;
                return status;
            }

            memcpy(tmp, b->data + offset + 1, slice_len);
            tmp[slice_len] = '\0';

            char* endptr;
            long len = strtol(tmp, &endptr, 10);

            if (endptr == tmp || endptr != tmp + slice_len || len < 0 ||
                len > INT_MAX) {
                out->status = INVALID;
                return status;
            }

            size_t s_len = (size_t)len;

            if (terminator + 2 + s_len + 2 > b->len) {
                out->status = INCOMPLETE;
                return status;
            }

            if (b->data[terminator + 2 + s_len] != '\r' ||
                b->data[terminator + 2 + s_len + 1] != '\n') {
                out->status = INVALID;
                return status;
            } else {
                char* copy = malloc(s_len + 1);

                if (copy == NULL) {
                    out->status = INVALID;
                    return status;
                }

                memcpy(copy, b->data + terminator + 2, s_len);
                copy[s_len] = '\0';
                out->argv[out->argc] = copy;
                out->argc++;

                status = true;
                *consumed = terminator + 2 + s_len + 2 - offset;
            }
        }
    }
    return status;
}

bool readArray(buffer_t* b, ParseResult* out) {

    bool status = false;
    if (b->len == 0) {
        out->status = INCOMPLETE;
        return status;
    } else if (b->data[0] != '*') {
        out->status = INVALID;
        return status;
    } else {
        int terminator = findTerminator(b, (size_t)0);
        if (terminator == -1) {
            out->status = INCOMPLETE;
            return status;
        } else {
            char tmp[16];
            size_t slice_len = terminator - 1;

            if (slice_len >= sizeof(tmp)) {
                out->status = INVALID;
                return status;
            }

            memcpy(tmp, b->data + 1, slice_len);
            tmp[slice_len] = '\0';

            char* endptr;
            long lamount = strtol(tmp, &endptr, 10);

            if (endptr == tmp || endptr != tmp + slice_len || lamount < 0 ||
                lamount > MAX_ARGS) {
                out->status = INVALID;
                return status;
            }

            int amount = (int)lamount;
            int callCount = 0;
            size_t consumed = (size_t)(terminator + 2);

            while (callCount < amount) {
                bool valid;
                size_t temp;
                valid = readBulk(b, out, consumed, &temp);
                callCount++;
                if (!valid) {
                    parseResult_free(out);
                    return status;
                }
                consumed += temp;
            }

            out->bytes_consumed = consumed;
            out->status = COMPLETE;
            status = true;
            return status;
        }
    }
}

ParseResult parse(buffer_t* b) {

    ParseResult result;
    parseResult_init(&result);

    readArray(b, &result);

    return result;
}
