#include "server.h"
#include "types.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char* argv[]) {

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return 1;
    }

    char* end;
    errno = 0;
    long port = strtol(argv[1], &end, 10);
    if (end == argv[1] || *end != '\0' || errno == ERANGE || port < 1 ||
        port > 65535) {
        fprintf(stderr, "port must be 1-65535\n");
        return 1;
    }

    return run_server((u16)port);
}
