#!/bin/bash
# Usage: tests/shutdown.sh [path-to-binary]   run from the repo root after `make`.
# Clean shutdown: SIGTERM/SIGINT must leave the loop, exit 0, keep the log
# intact, and (on Linux, where ASan includes LeakSanitizer) report no leaks.
P=6382
BIN=$(cd "$(dirname "${1:-./reddb}")" && pwd)/$(basename "${1:-./reddb}")
DIR=$(mktemp -d)
R="redis-cli -p $P"
FAILS=0

start() {
  (cd "$DIR" && exec "$BIN" $P >>"$DIR/server.log" 2>&1) &
  PID=$!
}
up() { for _ in $(seq 1 100); do
  $R PING >/dev/null 2>&1 && return
  sleep 0.05
done; }
stop() { # signal; sets CODE to the exit status, or "hung" after 3 s
  kill -"$1" $PID
  for _ in $(seq 1 30); do
    kill -0 $PID 2>/dev/null || break
    sleep 0.1
  done
  if kill -0 $PID 2>/dev/null; then
    kill -9 $PID
    wait $PID 2>/dev/null
    CODE=hung
  else
    wait $PID 2>/dev/null
    CODE=$?
  fi
}
check() { # name, expected, actual
  if [ "$2" == "$3" ]; then
    echo "ok    $1"
  else
    echo "FAIL  $1: expected '$2', got '$3'"
    FAILS=$((FAILS + 1))
  fi
}

# 1. idle server, SIGTERM
start
up
$R SET a 1 >/dev/null
stop TERM
check "SIGTERM exits 0" 0 "$CODE"

# 2. busy server: an idle client, a half-sent command, a pipeline in flight
start
up
check "data survives a clean restart" 1 "$($R GET a)"
exec 3<>/dev/tcp/127.0.0.1/$P # idle client
exec 4<>/dev/tcp/127.0.0.1/$P
printf '*3\r\n$3\r\nSET\r\n' >&4 # half a command
redis-benchmark -p $P -t set -n 200000 -c 20 -q >/dev/null 2>&1 &
BENCH=$!
sleep 0.3
stop INT
kill $BENCH 2>/dev/null
wait $BENCH 2>/dev/null
exec 3>&- 4>&-
check "SIGINT under load exits 0" 0 "$CODE"

# 3. signal arrives during replay (300k records + a + the benchmark's one key): whatever the server does, the log must not shrink
python3 -c "
import sys; w=sys.stdout.buffer
for i in range(300000):
    k=b'key:%d'%i; w.write(b'*3\r\n\$3\r\nSET\r\n\$%d\r\n%s\r\n\$16\r\nxxxxxxxxxxxxxxxx\r\n'%(len(k),k))
" >>"$DIR/appendonly.aof"
BEFORE=$(wc -c <"$DIR/appendonly.aof" | tr -d " ")
start
sleep 0.1
stop TERM
check "log intact after SIGTERM mid-replay" "$BEFORE" "$(wc -c <"$DIR/appendonly.aof" | tr -d ' ')"
start
up
check "and every key replays afterwards" 300002 "$($R DBSIZE)"
stop TERM

check "no sanitizer reports" 0 "$(grep -cE 'LeakSanitizer|AddressSanitizer|runtime error' "$DIR/server.log")"
echo "--- server log"
cat "$DIR/server.log"
[ $FAILS -eq 0 ] && echo "ALL PASSED" || echo "$FAILS FAILED"
rm -rf "$DIR"
