OS := $(shell uname -s)

CC = clang
COMMON_CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -Iinclude
SRC_CORE =  src/buffer.c src/resp.c \
      src/parser.c src/command.c src/hashtable.c  \
			src/mstime.c src/aof.c
SRC_NETWORK = src/main.c src/server.c
BIN = reddb

ifeq ($(OS), Darwin)
	EVENT_LOOP_SRC = src/el_kqueue.c
else 
	EVENT_LOOP_SRC =  src/el_epoll.c
endif

SRC_NETWORK += $(EVENT_LOOP_SRC)

SAN_CFLAGS = -fno-sanitize-recover=undefined -fsanitize=address,undefined -g 

all: debug

debug: 
	$(CC) $(COMMON_CFLAGS) $(SAN_CFLAGS)\
		-DREDDB_DEBUG -o $(BIN) $(SRC_CORE) $(SRC_NETWORK)

sanitize:
	$(CC) $(COMMON_CFLAGS) $(SAN_CFLAGS) -o $(BIN) $(SRC_CORE) $(SRC_NETWORK)

release:
	$(CC) $(COMMON_CFLAGS) -O2 -o $(BIN) $(SRC_CORE) $(SRC_NETWORK)

clean: 
	rm -f $(BIN)

.PHONY: all debug sanitize release clean
