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

## Tests:

`tests/ttl_vs_redis.sh` sends the same sequence of TTL commands to two servers and prints every reply, so a real Redis can serve as the reference:

```
redis-server --port 6390 --save '' --appendonly no --daemonize yes
./reddb 6380 &
diff <(./tests/ttl_vs_redis.sh 6390) <(./tests/ttl_vs_redis.sh 6380) && echo IDENTICAL
```

`tests/lost_reply.py` checks that a protocol error doesn't discard replies already queued for that client. It needs a 10 MB value first:

```
head -c 10000000 /dev/zero | tr '\0' x | redis-cli -p 6380 -x SET big
python3 tests/lost_reply.py
```

## Usage:

```
$ redis-cli -p 6380 SET name kyle EX 60
OK
$ sleep 1
$ redis-cli -p 6380 TTL name
(integer) 59
$ redis-cli -p 6380 GET name
"kyle"
$ redis-cli -p 6380 EXISTS name
(integer) 1
$ redis-cli -p 6380 EXPIRE name 10
(integer) 1
$ sleep 10
$ redis-cli -p 6380 EXISTS name
(integer) 0
$ redis-cli -p 6380 SET name kyle
OK
$ redis-cli -p 6380 DBSIZE
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
| `SET <key> <value>` | `+OK`, clears any ttl the key had |
| `SET <key> <value> EX <seconds>` | `+OK`, the key expires after `<seconds>` |
| `GET <key>` | the value, or the null bulk string if absent |
| `DEL <key>` | `1` if the key existed, `0` if not |
| `EXISTS <key>` | `1` or `0` |
| `INCR <key>` | the new value as an integer, keeps any ttl |
| `TTL <key>` | seconds left, rounded to nearest second; `-1` if the key has no expiry, `-2` if it doesn't exist |
| `EXPIRE <key> <seconds>` | `1` if key existed, `0` if not; `<seconds>` <= 0 deletes the key immediately |
| `DBSIZE` | the number of keys stored, including expired keys not yet reclaimed |

`INCR` on a missing key treats it as 0 and stores 1. On a value that isn't a
valid 64-bit integer, or one that would overflow, it replies
`ERR value is not an integer or out of range`. 
Integer parsing follows Redis's `string2ll` rather than C's `strtoll`, which is more permissive than it looks: `strtoll` skips leading whitespace and accepts a leading `+`, and neither it nor a naive check rejects redundant zeros. So `" 1"`, `"+1"`, `"01"`, `"-01"` and `"-0"` are all errors here, matching Redis — a value is a valid integer only if it's an optional `-` followed by a digit `1–9` and further digits, or the single character `0`. The same rule validates the seconds argument of `SET … EX` and `EXPIRE`. A valid integer that can't be used as a TTL (zero or negative for `SET … EX`, or large enough that the deadline would overflow) gets `ERR invalid expire time in '<command>' command` instead, matching Redis's split between the two errors.

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
src/mstime.c     mstime() function returns current time
tests/           redis-reply comparison and lost reply tests
```

**Protocol parsing is incremental.** One `recv()` is not one command: a command
may arrive split across several reads, and several pipelined commands may arrive
in a single read. Each connection owns an input buffer; the parser reports
`COMPLETE`, `INCOMPLETE`, `INVALID`, or `NOMEM` and only consumes bytes once a full
command has been read. A protocol error closes the connection, which is the only
recoverable response — there is no way to know where the next valid command
would start. The server stops reading from that client, finishes sending every reply already queued (including the `-ERR`), and closes once the output buffer drains.

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
NUL-terminated, but nothing relies on the terminator.

**`ht_get` returns a pointer into the table, not a copy.** Lookups are zero-copy.
The returned pointer is valid until the next `ht_*` call on that key, reads included, since a lookup that finds an expired key frees it. That is sufficient because the reply encoder copies the bytes into the output buffer immediately, and nothing stores a table pointer in a client.

**Growth rehashes by relinking existing nodes, not by reinserting them.** A
resize allocates a new bucket array and moves each node into its new bucket by
pointer assignment. Nothing is allocated or copied except the array itself. That
keeps resizing cheap, and it preserves the guarantee above: because entries never
move in memory, pointers returned by `ht_get` stay valid across a resize.

**The table owns its keys and values.** The parser frees each argument as soon as
dispatch returns, so the table copies both on insert and frees them on overwrite,
delete, and teardown.

**An expiry is an absolute deadline stored on the entry.** Each node carries
`expire_at`, wall-clock milliseconds since the Unix epoch, with `0` meaning no
expiry. A deadline stays correct without being updated, so checking it is one
comparison, and `TTL` converts back to seconds remaining. Wall-clock time rather
than a monotonic clock because the deadline has to mean the same instant after a
restart, once persistence exists. Keeping it on the node costs 8 bytes per key,
including keys with no TTL. Redis instead keeps a separate table of only the keys
that have one, which also lets its active expiry sample just those keys.

**The clock is read once per command.** `now` is passed into every `ht_*` call
instead of each lookup reading the clock itself, so a single command sees a
single instant. Without that, `INCR` could find a key alive in its lookup and
dead a millisecond later in its write. It also lets the table be tested with chosen times instead of sleeps.

**`SET` clears a TTL; `INCR` keeps it.** A plain `SET` replaces the key
entirely, while `INCR` changes only the number, matching Redis. Both go through
`ht_set`, which takes the new deadline, with `-1` meaning keep the existing one.
`-1` is only passed for a key the caller has just found alive.

**Expired keys are reclaimed lazily and actively.** Every lookup treats an
expired key as absent and frees it on the spot, which is what makes expiry
correct. To bound how long unread dead keys hold memory, the event loop also runs
a pass every 100 ms that scans the next 1000 buckets from a saved cursor and frees
whatever has expired. `el_poll` waits at most 100 ms so an idle server still runs
the pass, and the loop checks the clock after every batch of events so a busy one
does too. The fixed bucket budget keeps each pass short, at the cost that a full
sweep takes longer as the table grows.

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

- No persistence — the dataset is lost on shutdown.
- Expired keys can stay in memory until a lookup touches them or the active pass reaches their bucket, and `DBSIZE` counts them until then. A full sweep takes about capacity / 1000 passes of 100 ms each.
- Deadlines use the wall clock, so if the system clock jumps forward, keys expire early.
- Only a kqueue backend, so macOS and BSD. `el.h` is the seam an epoll backend
  would slot into.
- **No cap on a client's output buffer.** A client that pipelines requests
  without ever reading the replies will grow that buffer until the machine runs
  out of memory. This is deliberate for v1 and matches Redis's default for
  ordinary clients (`client-output-buffer-limit normal 0 0 0`).
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
- `SET` supports only `EX`. Other options (`PX`, `NX`, `XX`, `KEEPTTL`, …) are rejected with `ERR syntax error` rather than silently ignored.
- `EXPIRE` takes no options (`NX`/`XX`/`GT`/`LT`), and `PEXPIRE`, `PTTL` and `PERSIST` aren't implemented.
- A request may contain at most 32 arguments.
- The bucket array never shrinks, so the capacity stays at its high-water mark after
  deletions.

## Benchmarks

`redis-benchmark -t set,get -n 100000 -q`, reddb built with `make release` at commit `e734054` (before expiration was added),
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
price of what Redis does per command and reddb doesn't — ACLs,
replication and AOF hooks, keyspace notifications, RESP3. reddb has ten commands and none of it.
