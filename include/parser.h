#ifndef PARSER_H
#define PARSER_H
#include "buffer.h"
#include <stdbool.h>
typedef enum { COMPLETE, INCOMPLETE, INVALID } Status;

#define MAX_ARGS 32

typedef struct {
    Status status;
    int argc;
    char* argv[MAX_ARGS];
    size_t bytes_consumed;
} ParseResult;

ParseResult parse(buffer_t* b);
void parseResult_free(ParseResult* p);

void parseResult_init(ParseResult* p);
int findTerminator(buffer_t* b, size_t offset);
bool readBulk(buffer_t* b, ParseResult* out, size_t offset, size_t* consumed);
bool readArray(buffer_t* b, ParseResult* out);

#endif
