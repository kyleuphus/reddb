#ifndef AOF_H
#define AOF_H

#include "buffer.h"
#include "hashtable.h"
#include "types.h"

i32 aof_open(const char* path);

b8 aof_write_all(i32 fd, const char* data, usize len);

b8 aof_fsync(i32 fd);

void aof_append_command(buffer* b, const char** argv, const usize* arglen,
                        i32 argc);

b8 aof_load(const char* path, ht* db);
#endif
