#ifndef SERVER_H
#define SERVER_H

#include "buffer.h"
#include "hashtable.h"
#include "types.h"

typedef struct {
    i32 fd;
    i32 mask;       // what el_update last registerd
    buffer in;      // bytes read, not yet parsed
    buffer out;     // replies encoded, not yet sent
    usize out_sent; // how much of out is already sent
} client;

void client_init(client* c, i32 fd);

b8 client_process_input(client* c, ht* db);

int server_run(u16 port);

#endif
