#ifndef COMMAND_H
#define COMMAND_H
#include "buffer.h"
#include "hashtable.h"
#include "types.h"

void command_dispatch(char** argv, usize* arglen, i32 argc, buffer* out,
                      ht* t);
#endif
