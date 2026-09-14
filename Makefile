CC = clang
CFLAGS = -std=c17 -Wall -Wextra -Wpedantic \
         -fsanitize=address,undefined -g -Iinclude
SRC = src/main.c src/server.c src/buffer.c src/resp.c \
      src/parser.c src/command.c src/hashtable.c src/el_kqueue.c
BIN = reddb

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $(BIN) $(SRC)

debug: CFLAGS += -DREDDB_DEBUG
debug: $(BIN)

clean: 
	rm -f $(BIN)

.PHONY: clean debug
