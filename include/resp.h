#ifndef RESP_H
#define RESP_H
#include "buffer.h"
#include "types.h"

void resp_write_simple(buffer* out, const char* s);
void resp_write_error(buffer* out, const char* msg);
void resp_write_bulk(buffer* out, const char* s, usize len);
void resp_write_integer(buffer* out, i64 n);
void resp_write_null(buffer* out);

#endif
