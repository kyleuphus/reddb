#include "parser.h"

#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

void parse_result_init(parse_result* p) {
    p->status = PARSE_INCOMPLETE;
    p->argc = 0;
    for (i32 i = 0; i < MAX_ARGS; i++) {
        p->argv[i] = NULL;
    }
    for (i32 i = 0; i < MAX_ARGS; i++) {
        p->arglen[i] = 0;
    }
    p->bytes_consumed = 0;
}

void parse_result_free(parse_result* p) {
    for (i32 i = 0; i < p->argc; i++) {
        free(p->argv[i]);
        p->argv[i] = NULL;
    }
    p->argc = 0;
}

isize parse_find_crlf(buffer* b, usize offset) {

    for (usize i = offset; i + 1 < b->len; i++) {

        if (b->data[i] == '\r' && b->data[i + 1] == '\n') {
            return i;
        }
    }

    return -1;
}

b8 parse_bulk(buffer* b, parse_result* out, usize offset, usize* consumed) {

    b8 status = false;

    if (offset >= b->len) {
        out->status = PARSE_INCOMPLETE;
        return status;
    } else if (b->data[offset] != '$') {
        out->status = PARSE_INVALID;
        return status;
    } else {
        isize terminator = parse_find_crlf(b, offset);
        if (terminator == -1) {
            out->status = PARSE_INCOMPLETE;
            return status;
        } else {
            char tmp[16];
            usize slice_len = terminator - offset - 1;

            if (slice_len >= sizeof(tmp)) {
                out->status = PARSE_INVALID;
                return status;
            }

            memcpy(tmp, b->data + offset + 1, slice_len);
            tmp[slice_len] = '\0';

            char* endptr;
            long len = strtol(tmp, &endptr, 10);

            if (endptr == tmp || endptr != tmp + slice_len || len < 0 ||
                len > INT_MAX) {
                out->status = PARSE_INVALID;
                return status;
            }

            usize s_len = (usize)len;

            if (terminator + 2 + s_len + 2 > b->len) {
                out->status = PARSE_INCOMPLETE;
                return status;
            }

            if (b->data[terminator + 2 + s_len] != '\r' ||
                b->data[terminator + 2 + s_len + 1] != '\n') {
                out->status = PARSE_INVALID;
                return status;
            } else {
                char* copy = malloc(s_len + 1);

                if (copy == NULL) {
                    out->status = PARSE_INVALID;
                    return status;
                }

                memcpy(copy, b->data + terminator + 2, s_len);
                copy[s_len] = '\0';
                out->argv[out->argc] = copy;
                out->arglen[out->argc] = s_len;
                out->argc++;

                status = true;
                *consumed = terminator + 2 + s_len + 2 - offset;
            }
        }
    }
    return status;
}

b8 parse_array(buffer* b, parse_result* out) {

    b8 status = false;
    if (b->len == 0) {
        out->status = PARSE_INCOMPLETE;
        return status;
    } else if (b->data[0] != '*') {
        out->status = PARSE_INVALID;
        return status;
    } else {
        isize terminator = parse_find_crlf(b, (usize)0);
        if (terminator == -1) {
            out->status = PARSE_INCOMPLETE;
            return status;
        } else {
            char tmp[16];
            usize slice_len = terminator - 1;

            if (slice_len >= sizeof(tmp)) {
                out->status = PARSE_INVALID;
                return status;
            }

            memcpy(tmp, b->data + 1, slice_len);
            tmp[slice_len] = '\0';

            char* endptr;
            long raw_argc = strtol(tmp, &endptr, 10);

            if (endptr == tmp || endptr != tmp + slice_len || raw_argc < 0 ||
                raw_argc > MAX_ARGS) {
                out->status = PARSE_INVALID;
                return status;
            }

            i32 argc = (i32)raw_argc;
            i32 args_read = 0;
            usize consumed = (usize)(terminator + 2);

            while (args_read < argc) {
                b8 valid;
                usize bulk_consumed;
                valid = parse_bulk(b, out, consumed, &bulk_consumed);
                args_read++;
                if (!valid) {
                    parse_result_free(out);
                    return status;
                }
                consumed += bulk_consumed;
            }

            out->bytes_consumed = consumed;
            out->status = PARSE_COMPLETE;
            status = true;
            return status;
        }
    }
}

parse_result parse_command(buffer* b) {

    parse_result result;
    parse_result_init(&result);

    parse_array(b, &result);

    return result;
}
