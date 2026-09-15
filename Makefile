CC = clang
COMMON_CFLAGS = -std=c17 -Wall -Wextra -Wpedantic -Iinclude
SRC = src/main.c src/server.c src/buffer.c src/resp.c \
      src/parser.c src/command.c src/hashtable.c src/el_kqueue.c
BIN = reddb

all: debug

debug: 
	$(CC) $(COMMON_CFLAGS) -fsanitize=address,undefined -g \
		-DREDDB_DEBUG -o $(BIN) $(SRC)

release:
	$(CC) $(COMMON_CFLAGS) -O2 -o $(BIN) $(SRC)

clean: 
	rm -f $(BIN)

.PHONY: all debug release clean
