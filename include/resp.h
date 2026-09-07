#ifndef RESP_H
#define RESP_H
#include "buffer.h"

void resp_write_simple(buffer_t* out, const char* s);
void resp_write_error(buffer_t* out, const char* msg);
void resp_write_bulk(buffer_t* out, const char* s, size_t len);

#endif
