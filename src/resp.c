#include "resp.h"
#include <stdio.h>
#include <string.h>

void resp_write_simple(buffer_t* out, const char* s) {

    buf_append(out, "+", strlen("+"));
    buf_append(out, s, strlen(s));
    buf_append(out, "\r\n", strlen("\r\n"));
}

void resp_write_error(buffer_t* out, const char* msg) {

    buf_append(out, "-", strlen("-"));
    buf_append(out, msg, strlen(msg));
    buf_append(out, "\r\n", strlen("\r\n"));
}
void resp_write_bulk(buffer_t* out, const char* s, size_t len) {

    buf_append(out, "$", strlen("$"));

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%zu\r\n", len);
    buf_append(out, buffer, strlen(buffer));

    buf_append(out, s, len);
    buf_append(out, "\r\n", strlen("\r\n"));
}
