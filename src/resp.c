#include "resp.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

void resp_write_simple(buffer* out, const char* s) {

    buf_append(out, "+", strlen("+"));
    buf_append(out, s, strlen(s));
    buf_append(out, "\r\n", strlen("\r\n"));
}

void resp_write_error(buffer* out, const char* msg) {

    buf_append(out, "-", strlen("-"));
    buf_append(out, msg, strlen(msg));
    buf_append(out, "\r\n", strlen("\r\n"));
}

void resp_write_bulk(buffer* out, const char* s, usize len) {

    buf_append(out, "$", strlen("$"));

    char header[32];
    i32 written = snprintf(header, sizeof(header), "%zu\r\n", len);
    buf_append(out, header, (usize)written);

    buf_append(out, s, len);
    buf_append(out, "\r\n", strlen("\r\n"));
}

void resp_write_integer(buffer* out, i64 n) {
    char line[32];
    i32 written = snprintf(line, sizeof(line), ":%" PRId64 "\r\n", n);
    buf_append(out, line, (usize)written);
}

void resp_write_null(buffer* out) {
    buf_append(out, "$-1\r\n", strlen("$-1\r\n"));
}
