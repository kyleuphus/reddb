#include "server.h"
#include "buffer.h"
#include "command.h"
#include "hashtable.h"
#include "parser.h"
#include "resp.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int run_server(u16 port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(fd, 16) < 0) {
        perror("listen");
        return 1;
    }

    printf("listening on port %d\n", port);

    ht* t = ht_create();

    for (;;) {

        int client_fd = accept(fd, NULL, NULL);

        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        printf("got a client, fd = %d\n", client_fd);

        buffer_t in;
        buf_init(&in);
        buffer_t out;
        buf_init(&out);

        char scratch[1024];
        ssize_t n;

        while ((n = recv(client_fd, scratch, sizeof(scratch), 0)) > 0) {
            buf_append(&in, scratch, (usize)n);
            b8 valid = true;
            for (;;) {
                ParseResult r = parse(&in);
                if (r.status == INCOMPLETE) {
                    parseResult_free(&r);
                    break;
                } else if (r.status == INVALID) {
                    resp_write_error(&out, "ERR could not be parsed");
                    valid = false;
                    parseResult_free(&r);
                    break;
                } else {
                    dispatch(r.argv, r.arglen, r.argc, &out, t);
                    buf_consume(&in, r.bytes_consumed);
                }
                parseResult_free(&r);
            }
            if (out.len > 0) {
                send(client_fd, out.data, out.len, 0);
                out.len = 0;
            }
            if (!valid) {
                break;
            }
        }

        if (n < 0) {
            perror("recv");
        }

        buf_free(&in);
        buf_free(&out);

        close(client_fd);
    }

    return 0;
}
