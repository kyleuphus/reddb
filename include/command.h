#ifndef COMMAND_H
#define COMMAND_H
#include "buffer.h"
#include "types.h"

void dispatch(char** argv, usize* arglen, i32 argc, buffer_t* out);
#endif
