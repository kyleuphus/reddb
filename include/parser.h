#ifndef PARSER_H
#define PARSER_H

#include "buffer.h"
#include "types.h"
#include <stdbool.h>

typedef enum { COMPLETE, INCOMPLETE, INVALID } Status;

#define MAX_ARGS 32

typedef struct {
    Status status;
    i32 argc;
    char* argv[MAX_ARGS];
    usize bytes_consumed;
} ParseResult;

ParseResult parse(buffer_t* b);
void parseResult_free(ParseResult* p);

void parseResult_init(ParseResult* p);
isize findTerminator(buffer_t* b, usize offset);
b8 readBulk(buffer_t* b, ParseResult* out, usize offset, usize* consumed);
b8 readArray(buffer_t* b, ParseResult* out);

#endif
