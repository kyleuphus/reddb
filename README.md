# reddb

A Redis-compatible in-memory key-value database written from scratch in C. Communicates 
by RESP2 over TCP, and serves many clients concurrently from a single thread.

## Build and run:

```
make          # sanitizers + debug logging - the default target
./reddb 6380

make release  # -O2, no sanitizers - for benchmarking
```

Requires clang or gcc with C17. Both builds compile with `-Wall -Wextra
-Wpedantic`; the default adds AddressSanitizer and UndefinedBehaviorSanitizer.
Both targets rebuild from source, so switching between them needs no `make
clean`.

The event loop currently has only a kqueue backend, so the build targets macOS
and BSD. Linux needs an epoll backend behind `el.h`, which is what that
interface exists for.

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

Clients are served concurrently. An idle connection doesn't hold up anyone else:

```
# terminal 1                      # terminal 2
$ redis-cli -p 6380               $ redis-cli -p 6380 PING
127.0.0.1:6380> SET a 1           PONG        <- answered immediately
OK
```

Replies larger than the socket's send buffer are delivered across as many writes
as it takes:

```
$ head -c 10000000 /dev/zero | tr '\0' x | redis-cli -p 6380 -x SET big
OK
$ redis-cli -p 6380 GET big | wc -c
10000001
```

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
include/el.h     event loop interface: register read/write interest, poll for readiness
src/el_kqueue.c  kqueue backend for el.h
src/main.c       argument handling, entry point
src/server.c     TCP socket setup, event loop, per-client state, read and write paths
src/parser.c     incremental RESP2 request parser
src/command.c    command dispatch
src/hashtable.c  the key-value store
src/resp.c       RESP2 reply encoders
src/buffer.c     growable byte buffer
```

**Protocol parsing is incremental.** One `recv()` is not one command: a command
may arrive split across several reads, and several pipelined commands may arrive
in a single read. Each connection owns an input buffer; the parser reports
`COMPLETE`, `INCOMPLETE`, `INVALID`, or `NOMEM` and only consumes bytes once a full
command has been read. A protocol error closes the connection, which is the only
recoverable response — there is no way to know where the next valid command
would start.

**The hash table** uses separate chaining: an array of buckets, each holding a
singly linked list of entries whose keys hashed there. Keys are hashed with
FNV-1a over exactly their byte length. The bucket count is always a power of two,
so the index is a mask rather than a modulo. The table starts at 4 buckets and
doubles when the load factor reaches 0.5.

**The event loop** asks the kernel which file descriptors are ready and only touches those.
Each connection's state lives in a struct, and the commands run one at a time, so 
the table does not need locking.

TCP is a byte stream, not a sequence of messages, so a command can arrive in
pieces and several can arrive at once. The incremental parser is what makes that
survivable: a client that has sent half a command simply leaves it in its input
buffer, and the loop moves on to whoever else is ready. The same holds in
reverse — a reply too large for the socket's send buffer goes out across as many
`send` calls as the kernel accepts, without blocking the other connections.


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

**The kernel is reached only through `el.h`, so a second backend is a new file.**
`server.c` holds an opaque `el_loop` and never sees a `kevent` or an
`epoll_event`, so porting to Linux means writing `el_epoll.c` and changing
nothing else. This is the structure Redis uses (`ae.c`, with `ae_kqueue.c` and
`ae_epoll.c` behind it). The interface detail that makes it work is that
`el_update` takes both the old and the new interest mask: epoll needs the old one
to choose between ADD, MOD, and DEL, and kqueue needs the difference to know
which filters to add and which to delete. With the caller owning the mask, both
backends stay stateless.

**One `recv` per wakeup is enough, because the loop is level-triggered.** Both
kqueue and epoll report a socket as ready whenever unread bytes are sitting in
it, not only at the moment new bytes arrive, so a read that stops early is simply
reported again on the next poll. That makes a fixed 16 KB read buffer safe
against a request of any size, and it keeps the loop fair: a client sending 10 MB
returns to `el_poll` after every chunk, so other connections are served in
between rather than waiting for it to finish.

**Replies track an offset, and write interest is registered only when needed.**
`send` on a nonblocking socket takes as much as fits in the kernel's buffer and
reports how much that was, so a client records how far into its output buffer it
has got rather than consuming from the front — which would `memmove` the
remaining megabytes after every call. Write interest is the mirror of the
level-triggered tradeoff above: a socket with room in its send buffer is *always*
writable, so registering for it up front would make `el_poll` return immediately,
forever. It is registered only when a `send` comes up short, and dropped the
moment the buffer drains.



## Limitations

- No key expiration and no persistence — the dataset is lost on shutdown.
- Only a kqueue backend, so macOS and BSD. `el.h` is the seam an epoll backend
  would slot into.
- **No cap on a client's output buffer.** A client that pipelines requests
  without ever reading the replies will grow that buffer until the machine runs
  out of memory.
- A protocol error closes the connection immediately, so the `-ERR` reply can be
  lost if the send is partial.
- Inline commands are not supported — the parser accepts only RESP arrays, so
  typing commands straight into `nc` or `telnet` is a protocol error and
  disconnects.
- `accept` failing with `EMFILE` spins: the listener is level-triggered and stays
  readable, so the loop retries as fast as it can until a descriptor frees up.
- At most 1024 concurrent clients; higher file descriptors are refused and
  closed.
- No clean shutdown path — the process exits without freeing the table or the
  remaining clients, so a leak check has no normal exit to report at.
- `DEL` and `EXISTS` take a single key. Real Redis accepts several and returns
  how many matched; the reply here is already shaped as a count, so multi-key
  support is a loop rather than a rewrite.
- `SET` supports no options (`EX`, `NX`, etc. are rejected as an arity
  error rather than silently ignored).
- A request may contain at most 32 arguments.
- The bucket array never shrinks, so the capacity stays at its high-water mark after
  deletions.

## Benchmarks

`redis-benchmark -t set,get -n 100000 -q`, reddb built with `make release`,
Redis with persistence off, on an M4 MacBook Pro over loopback. SET / GET
requests per second:

| clients | reddb | Redis |
|---:|---:|---:|
| 1 | 68k / 71k | 64k / 67k |
| 50 | 240k / 241k | 250k / 251k |
| 500 | 219k / 233k | 230k / 227k |

Both flatten near 235k from ten clients up — the benchmark saturates before
either server does. Throughput at 500 clients is within a few percent of 10.

Adding `-P 16` to pipeline sixteen commands per round trip removes the syscall
cost and reddb reaches 3.1M / 3.2M against Redis's 1.9M / 2.0M. That gap is the
price of what Redis does per command and reddb doesn't — expiration checks, ACLs,
replication and AOF hooks, RESP3. reddb has eight commands and none of it.
