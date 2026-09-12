#ifndef PARSER_H
#define PARSER_H

#include "buffer.h"
#include "types.h"
#include <stdbool.h>

typedef enum { PARSE_COMPLETE, PARSE_INCOMPLETE, PARSE_INVALID } parse_status;

#define MAX_ARGS 32

typedef struct {
    parse_status status;
    i32 argc;
    char* argv[MAX_ARGS];
    usize arglen[MAX_ARGS];
    usize bytes_consumed;
} parse_result;

parse_result parse_command(buffer* b);
void parse_result_free(parse_result* p);

void parse_result_init(parse_result* p);
isize parse_find_crlf(buffer* b, usize offset);
b8 parse_bulk(buffer* b, parse_result* out, usize offset, usize* consumed);
b8 parse_array(buffer* b, parse_result* out);

#endif
