# reddb

A Redis-compatible in-memory key-value database written from scratch in C.
It speaks RESP2 over TCP, so standard Redis clients can talk to it without modification.

No third-party libraries. The parser, hash table, and server
loop are all hand-written.

## Build and run:

```
make
./reddb 6380
```

Requires clang or gcc with C17. The build enables `-Wall -Wextra -Wpedantic`
and AddressSanitizer/UndefinedBehaviorSanitizer by default.

## Usage:

```
$ redis-cli -p 6380 SET name kyle
OK
$ redis-cli -p 6380 GET name
"kyle"
$ redis-cli -p 6380 EXISTS name
(integer) 1
$ redis-cli -p 6380 DEL name
(integer) 1
$ redis-cli -p 6380 GET name
(nil)
$ redis-cli -p 6380 INCR counter
(integer) 1
```

Each of those is a separate TCP connection; the data persists in the server independently of
the client that wrote it.

## Commands

| Command | Reply |
|---|---|
| `PING` | `+PONG` |
| `PING <msg>` | the message, as a bulk string |
| `ECHO <msg>` | the message, as a bulk string |
| `SET <key> <value>` | `+OK` |
| `GET <key>` | the value, or the null bulk string if absent |
| `DEL <key>` | `1` if the key existed, `0` if not |
| `EXISTS <key>` | `1` or `0` |
| `INCR <key>` | the new value as an integer |

`INCR` on a missing key treats it as 0 and stores 1. On a value that isn't a
valid 64-bit integer, or one that would overflow, it replies
`ERR value is not an integer or out of range`. 
Integer parsing follows Redis's `string2ll` rather than C's `strtoll`, which is
more permissive than it looks: `strtoll` skips leading whitespace and accepts a
leading `+`, and neither it nor a naive check rejects redundant zeros. So `" 1"`,
`"+1"`, `"01"`, `"-01"` and `"-0"` are all errors here, matching Redis — a value
is a valid integer only if it's an optional `-` followed by a digit `1–9` and
further digits, or the single character `0`.

## Architecture

```
src/main.c       argument handling, entry point
src/server.c     TCP socket setup, accept loop, per-connection read loop
src/parser.c     incremental RESP2 request parser
src/command.c    command dispatch
src/hashtable.c  the key-value store
src/resp.c       RESP2 reply encoders
src/buffer.c     growable byte buffer
```

**Protocol parsing is incremental.** One `recv()` is not one command: a command
may arrive split across several reads, and several pipelined commands may arrive
in a single read. Each connection owns an input buffer; the parser reports
`COMPLETE`, `INCOMPLETE`, or `INVALID`, and only consumes bytes once a full
command has been read. A protocol error closes the connection, which is the only
recoverable response — there is no way to know where the next valid command
would start.

**The hash table** uses separate chaining: an array of buckets, each holding a
singly linked list of entries whose keys hashed there. Keys are hashed with
FNV-1a over exactly their byte length. The bucket count is always a power of two,
so the index is a mask rather than a modulo. The table starts at 4 buckets and
doubles when the load factor reaches 0.5.

## Design decisions

**Keys and values carry explicit lengths.** Every string is a `(pointer, length)`
pair from the parser through dispatch and into the table, and key comparison is
length equality followed by `memcmp`. This makes the store binary-safe: a value
containing a zero byte round-trips correctly, which Redis permits and a
`strlen`-based implementation silently truncates. Stored copies are also
NUL-terminated as a convenience for `INCR`'s `strtoll`, but the length is what
every comparison and reply uses.

**`ht_get` returns a pointer into the table, not a copy.** Lookups are zero-copy.
The returned pointer is valid until the next operation that modifies that key,
which is sufficient because the reply encoder copies the bytes into the output
buffer immediately. The alternative — returning a freshly allocated copy the
caller must free — costs an allocation per read to remove a hazard that doesn't
currently exist.

**Growth rehashes by relinking existing nodes, not by reinserting them.** A
resize allocates a new bucket array and moves each node into its new bucket by
pointer assignment. Nothing is allocated or copied except the array itself. That
keeps resizing cheap, and it preserves the guarantee above: because entries never
move in memory, pointers returned by `ht_get` stay valid across a resize.

**The table owns its keys and values.** The parser frees each argument as soon as
dispatch returns, so the table copies both on insert and frees them on overwrite,
delete, and teardown.


## Limitations

- **One client at a time.** Blocking I/O and a sequential accept loop; a second
  client waits until the first disconnects. Concurrency is next.
- No key expiration and no persistence — the dataset is lost on shutdown.
- `DEL` and `EXISTS` take a single key. Real Redis accepts several and returns
  how many matched; the reply here is already shaped as a count, so multi-key
  support is a loop rather than a rewrite.
- `SET` supports no options (`EX`, `NX`, etc. are rejected as an arity
  error rather than silently ignored).
- A request may contain at most 32 arguments.
- The bucket array never shrinks, so the capacity stays at its high-water mark after
  deletions.
