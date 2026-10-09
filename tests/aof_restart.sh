#!/bin/bash
# Usage: tests/aof_restart.sh [path-to-binary]   run from the repo root after `make`.
# Starts the server in a fresh temp dir on port 6381, kills it with SIGKILL
# (no chance to clean up), restarts it, and checks what came back.
P=6381
BIN=$(cd "$(dirname "${1:-./reddb}")" && pwd)/$(basename "${1:-./reddb}")
DIR=$(mktemp -d)
R="redis-cli -p $P"
FAILS=0

start() {
  (cd "$DIR" && exec "$BIN" $P >>"$DIR/server.log" 2>&1) &
  PID=$!
  sleep 0.5
}
crash() {
  kill -9 $PID 2>/dev/null
  wait $PID 2>/dev/null
}
check() { # name, expected, actual
  if [ "$2" == "$3" ]; then
    echo "ok    $1"
  else
    echo "FAIL  $1: expected '$2', got '$3'"
    FAILS=$((FAILS + 1))
  fi
}

start
$R SET foo bar >/dev/null
$R SET k v EX 100 >/dev/null
$R SET c 1 EX 2 >/dev/null
$R INCR c >/dev/null
$R INCR c >/dev/null
$R SET e v >/dev/null
$R EXPIRE e 2 >/dev/null
$R SET x notint >/dev/null
$R INCR x >/dev/null
$R SET gone v >/dev/null
$R DEL gone >/dev/null
sleep 2.2
crash
start
check "SET survives kill -9" bar "$($R GET foo)"
T=$($R TTL k) # ~3 s have passed since SET k v EX 100
check "deadline is absolute, not reset" 1 "$([ "$T" -ge 90 ] && [ "$T" -le 98 ] && echo 1)"
check "INCR'd key expired while down" "" "$($R GET c)"
check "EXPIRE'd key expired while down" 0 "$($R EXISTS e)"
check "failed INCR changed nothing" notint "$($R GET x)"
check "DEL replayed" 0 "$($R EXISTS gone)"

crash
printf '*3\r\n$3\r\nSET\r\n$4\r\nhalf' >>"$DIR/appendonly.aof" # a write cut off mid-record
SIZE_BEFORE=$(wc -c <"$DIR/appendonly.aof")
start
check "starts despite torn last record" bar "$($R GET foo)"
check "torn record cut from the file" 1 "$([ $(wc -c <"$DIR/appendonly.aof") -lt $SIZE_BEFORE ] && echo 1)"
$R SET after tear >/dev/null
crash
start
check "writes after the repair replay" tear "$($R GET after)"

crash
printf 'garbage\r\n' >>"$DIR/appendonly.aof" # corruption, not a torn tail
start
check "refuses to start on corruption" "" "$($R PING 2>/dev/null)"
crash

echo "--- server log"
cat "$DIR/server.log"
[ $FAILS -eq 0 ] && echo "ALL PASSED" || echo "$FAILS FAILED"
rm -rf "$DIR"
