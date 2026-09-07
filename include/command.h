#ifndef COMMAND_H
#define COMMAND_H
#include "buffer.h"

void dispatch(char** argv, int argc, buffer_t* out);

#endif
