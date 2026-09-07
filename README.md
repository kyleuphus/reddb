# reddb

A Redis-compatible server written in C. It speaks RESP2 over TCP, so standard
Redis clients can talk to it.

## Build and run:

make
./reddb 6380

## Testing it:

$ redis-cli -p 6380 PING
PONG
$ redis-cli -p 6380 ECHO hello
"hello"

## What works

- RESP2 request parsing (arrays of bulk strings) and reply encoding
- Incremental parsing — commands split across TCP reads, and multiple commands
  pipelined into a single read, are both handled correctly
- `PING`, `PING <msg>`, `ECHO <msg>`, with error replies for unknown commands
  and wrong argument counts
- One client at a time, blocking I/O
