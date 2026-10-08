#!/bin/bash
# Usage: tests/ttl_vs_redis.sh <port>   prints every reply; diff two runs.
# Takes ~4 s (three sleeps). Safe to re-run: every key it reads it sets first.
P=${1:-6380}
R="redis-cli -p $P"
echo "--- INCR keeps TTL, SET clears it"
$R SET c 1 EX 100
$R INCR c
$R TTL c
$R SET c 5
$R TTL c
echo "--- TTL missing / no expiry"
$R TTL nope
$R SET p v
$R TTL p
echo "--- EXPIRE"
$R EXPIRE nope 10
$R EXPIRE p 10
$R TTL p
$R SET q v
$R EXPIRE q 0
$R EXISTS q
$R SET q v
$R EXPIRE q -5
$R EXISTS q
echo "--- EXPIRE bad int"
$R EXPIRE p abc
$R EXPIRE p " 10"
$R EXPIRE p 01
$R EXPIRE p 9223372036854775807
echo "--- SET EX"
$R SET s v EX 0
$R SET s v EX -1
$R SET s v EX abc
$R SET s v EX
$R SET s v ex 10
$R TTL s
$R SET s v FOO 1
$R SET s v EX 9223372036854775807
echo "--- expiry happens"
$R SET e v EX 1
$R GET e
sleep 1.2
$R GET e
$R TTL e
$R EXISTS e
echo "--- DEL on expired"
$R SET d v EX 1
sleep 1.2
$R DEL d
echo "--- INCR on expired"
$R SET i 9 EX 1
sleep 1.2
$R INCR i
$R TTL i
