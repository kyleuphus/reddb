#include "server.h"
#include "buffer.h"
#include "command.h"
#include "hashtable.h"
#include "parser.h"
#include "resp.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

void client_init(client* c, i32 fd) {
    c->mask = 0;
    c->fd = fd;
    buf_init(&c->in);
    buf_init(&c->out);
    c->out_sent = 0;
}

b8 client_process_input(client* c, ht* db) {
    b8 valid = true;
    for (;;) {
        parse_result r = parse_command(&c->in);
        if (r.status == PARSE_INCOMPLETE) {
            parse_result_free(&r);
            break;
        } else if (r.status == PARSE_INVALID) {
            resp_write_error(&c->out, "ERR could not be parsed");
            valid = false;
            parse_result_free(&r);
            break;
        } else {
            command_dispatch(r.argv, r.arglen, r.argc, &c->out, db);
            buf_consume(&c->in, r.bytes_consumed);
        }
        parse_result_free(&r);
    }
    return valid;
}

int server_run(u16 port) {
    signal(SIGPIPE, SIG_IGN);
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

    ht* db = ht_create();

    for (;;) {

        int client_fd = accept(fd, NULL, NULL);

        if (client_fd < 0) {
            perror("accept");
            continue;
        }

        printf("got a client, fd = %d\n", client_fd);

        client c;
        client_init(&c, client_fd);

        char scratch[1024 * 16];
        ssize_t n;

        while ((n = recv(client_fd, scratch, sizeof(scratch), 0)) > 0) {
            buf_append(&c.in, scratch, (usize)n);

            b8 valid = client_process_input(&c, db);

            if (c.out.len > 0) {
                send(client_fd, c.out.data, c.out.len, 0);
                c.out.len = 0;
            }
            if (!valid) {
                break;
            }
        }

        if (n < 0) {
            perror("recv");
        }

        buf_free(&c.in);
        buf_free(&c.out);

        close(client_fd);
    }

    return 0;
}
