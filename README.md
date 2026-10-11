# reddb

A Redis-compatible in-memory key-value database written from scratch in C. Communicates 
by RESP2 over TCP, serves many clients concurrently from a single thread, and persists
every write to an append-only log that is replayed on startup.

## Build and run:

```
make          # sanitizers + debug logging - the default target
./reddb 6380

make release  # -O2, no sanitizers - for benchmarking
```

The server keeps its log in `appendonly.aof` in the directory it was started
from, creating it on the first run. Delete that file to start with an empty database.

Requires clang or gcc with C17. Both builds compile with `-Wall -Wextra
-Wpedantic`; the default adds AddressSanitizer and UndefinedBehaviorSanitizer.
Both targets rebuild from source, so switching between them needs no `make
clean`.

The event loop currently has only a kqueue backend, so the build targets macOS
and BSD. Linux needs an epoll backend behind `el.h`, which is what that
interface exists for.

## Tests:

`tests/aof_restart.sh` checks persistence. It runs the server in a temporary directory
on port 6381, kills it with `kill -9` (no chance to clean up), then restarts it and 
checks what came back: plain values, TTLs that expired while the server was down, a failed
command that must not have been logged, a half-written last record, and a corrupted log.

```
make && ./tests/aof_restart.sh     # prints ALL PASSED
```

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
| `SET <key> <value> PXAT <ms>` | `+OK`, the key expires at the absolute Unix time `<ms>` |
| `GET <key>` | the value, or the null bulk string if absent |
| `DEL <key>` | `1` if the key existed, `0` if not |
| `EXISTS <key>` | `1` or `0` |
| `INCR <key>` | the new value as an integer, keeps any ttl |
| `TTL <key>` | seconds left, rounded to nearest second; `-1` if the key has no expiry, `-2` if it doesn't exist |
| `EXPIRE <key> <seconds>` | `1` if key existed, `0` if not; `<seconds>` <= 0 deletes the key immediately |
| `PEXPIREAT <key> <ms>` | `1` if key existed, `0` if not; sets an absolute deadline in Unix milliseconds; `<ms>` <= 0 deletes the key |
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
src/aof.c        append-only log: record encoding, writes, fsync, startup replay
tests/           redis-reply comparison, lost reply, and kill -9 restart tests
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

**Keys and values carry explicit lengths.** Every string is a `(pointer, length)` pair compared with `memcmp`, so the store is binary-safe where `strlen` would truncate at a zero byte.

**`ht_get` returns a pointer into the table, not a copy.** It stays valid until the next `ht_*` call on that key, which is enough because replies copy the bytes immediately.

**Growth rehashes by relinking existing nodes, not by reinserting them.** Only the bucket array is reallocated, so resizing is cheap and `ht_get` pointers survive it.

**The table owns its keys and values.** It copies them on insert and frees them on overwrite, delete and teardown, because the parser frees each argument once dispatch returns.

**An expiry is an absolute deadline stored on the entry.** Wall-clock milliseconds mean the same instant after a restart and take one comparison to check, at a cost of 8 bytes on every key.

**The clock is read once per command.** `now` is passed into every `ht_*` call, so one command sees one instant and the table can be tested with chosen times.

**`SET` clears a TTL; `INCR` keeps it.** Both go through `ht_set`, which takes the new deadline, with `-1` meaning keep the existing one.

**Expired keys are reclaimed lazily and actively.** Lookups free expired keys on the spot, and a pass every 100 ms scans the next 1000 buckets to bound how long unread ones hold memory.

**The kernel is reached only through `el.h`, so a second backend is a new file.** `el_update` takes both the old and new interest mask, which keeps the kqueue and epoll backends stateless.

**One `recv` per wakeup is enough, because the loop is level-triggered.** A socket with unread bytes is reported again on the next poll, so a fixed 16 KB read handles any request size and no client can hog the loop.

**Replies track an offset, and write interest is registered only when needed.** Consuming from the front would `memmove` the rest of the buffer on every `send`, and a socket with room is always writable, so permanent write interest would spin.

**The log is the wire protocol.** Each write is appended as the RESP array a client would send, so replay reuses the incremental parser and `command_dispatch`.

**Only successful changes are logged.** `command_dispatch` appends the record itself, and replay passes `NULL` so it never re-logs what it replays.

**Deadlines are written as absolute times.** `SET … EX` is logged as `SET … PXAT` and `EXPIRE` as `PEXPIREAT`, so a restart doesn't restart every countdown.

**Replay runs with the clock stopped.** With `now = 0` nothing expires mid-replay, which would otherwise let a later `INCR` recreate an expired key with no TTL.

**A record is written before its reply is sent.** No client is told a write succeeded before it's in the log, and a failed write exits the server so memory and log agree on restart.

**A torn last record is cut; anything else unreadable stops startup.** Appending after a partial record would corrupt the next write, while garbage mid-file means the log isn't one this server wrote.

**The log is fsynced once a second.** A power cut loses at most about a second of writes, and macOS uses `fcntl(F_FULLFSYNC)` because its `fsync` stops at the drive's cache.

**Replay happens before the port is bound.** No client can connect to a half-built table.

## Limitations

- Expired keys hold memory until a lookup or the active pass reaches them, and `DBSIZE` counts them until then.
- Deadlines use the wall clock, so a forward clock jump expires keys early.
- Only a kqueue backend (macOS and BSD); an epoll backend would slot in behind `el.h`.
- **No cap on a client's output buffer**, so a client that never reads its replies can exhaust memory, the same as Redis's default for ordinary clients.
- Inline commands aren't supported, so typing commands into `nc` or `telnet` is a protocol error.
- `accept` failing with `EMFILE` spins until a descriptor frees up, because the listener stays readable.
- At most 1024 concurrent clients; higher file descriptors are refused.
- No clean shutdown path, so leak checkers have no normal exit to report at.
- `DEL` and `EXISTS` take a single key, where Redis accepts several.
- `SET` supports only `EX` and `PXAT`; other options are rejected with `ERR syntax error`.
- `EXPIRE` takes no options, and `PEXPIRE`, `PTTL` and `PERSIST` aren't implemented.
- A request may contain at most 32 arguments.
- The bucket array never shrinks after deletions.
- A power cut can lose up to about a second of acknowledged writes; the restart test uses `kill -9`, so it can't test this.
- The once-a-second `fsync` runs on the event loop, so every client waits for it.
- The log only grows; there is no rewrite or compaction.
- The log is always `appendonly.aof` in the working directory.
- A newly created log's directory entry isn't fsynced.

## Benchmarks

`redis-benchmark -t set,get -n 100000 -q`, reddb built with `make release` at commit `e734054` (before expiration and persistence were added),
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
replication, keyspace notifications, RESP3. reddb has eleven commands and none of it.
