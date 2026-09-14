#include "server.h"
#include "buffer.h"
#include "command.h"
#include "debug.h"
#include "el.h"
#include "hashtable.h"
#include "resp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_CLIENTS 1024
static client* clients[MAX_CLIENTS];

static void close_client(el_loop* loop, client* c) {
    el_update(loop, c->fd, c->mask, EL_NONE);
    close(c->fd);
    clients[c->fd] = NULL;
    buf_free(&c->in);
    buf_free(&c->out);
    free(c);
}

static b8 set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return false;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static b8 read_client(client* c, ht* db) {

    char scratch[1024 * 16];
    ssize n;

    n = recv(c->fd, scratch, sizeof(scratch), 0);

    if (n > 0) {
        buf_append(&c->in, scratch, (usize)n);

        parse_status status = client_process_input(c, db);

        if (c->out.len > 0) {
            send(c->fd, c->out.data, c->out.len, 0);
            c->out.len = 0;
        }

        if (status == PARSE_NOMEM || status == PARSE_INVALID) {
            return false;
        }

        return true;
    } else if (n == 0) {
        return false;
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return true;
        } else {
            return false;
        }
    }
}

static void accept_clients(el_loop* loop, i32 listen_fd) {
    DEBUG_LOG("accepting client fd=%d\n", listen_fd);
    for (;;) {

        i32 client_fd = accept(listen_fd, NULL, NULL);

        if (client_fd < 0) {

            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }

            if (errno == EINTR) {
                continue;
            }

            perror("accept");
            break;
        }

        if (client_fd >= MAX_CLIENTS) {
            close(client_fd);
            continue;
        }

        set_nonblocking(client_fd);

        client* c = calloc(1, sizeof(client));

        if (c == NULL) {
            perror("calloc");
            close(client_fd);
            break;
        }

        client_init(c, client_fd);
        clients[client_fd] = c;

        el_update(loop, client_fd, EL_NONE, EL_READABLE);
        c->mask = EL_READABLE;
        DEBUG_LOG("accepted fd=%d\n", client_fd);
    }
}

void client_init(client* c, i32 fd) {
    c->mask = 0;
    c->fd = fd;
    buf_init(&c->in);
    buf_init(&c->out);
    c->out_sent = 0;
}

parse_status client_process_input(client* c, ht* db) {
    parse_status status = PARSE_INCOMPLETE;
    for (;;) {
        parse_result r = parse_command(&c->in);
        if (r.status == PARSE_INCOMPLETE) {
            parse_result_free(&r);
            break;
        } else if (r.status == PARSE_INVALID) {
            resp_write_error(&c->out, "ERR could not be parsed");
            parse_result_free(&r);
            status = PARSE_INVALID;
            break;
        } else if (r.status == PARSE_NOMEM) {
            resp_write_error(&c->out, "ERR out of memory");
            parse_result_free(&r);
            status = PARSE_NOMEM;
            break;
        } else {
            command_dispatch(r.argv, r.arglen, r.argc, &c->out, db);
            buf_consume(&c->in, r.bytes_consumed);
        }
        parse_result_free(&r);
    }
    return status;
}

int server_run(u16 port) {
    signal(SIGPIPE, SIG_IGN);
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(listen_fd, 16) < 0) {
        perror("listen");
        return 1;
    }

    printf("listening on port %d\n", port);

    set_nonblocking(listen_fd);

    ht* db = ht_create();
    el_loop* loop = el_create();
    if (loop == NULL) {
        perror("loop");
        return 1;
    }
    el_update(loop, listen_fd, EL_NONE, EL_READABLE);
    el_fired fired[EL_MAX_EVENTS];

    for (;;) {

        i32 n = el_poll(loop, fired, -1);

        if (n < 0) {
            perror("el_poll");
            break;
        }

        for (i32 i = 0; i < n; i++) {

            DEBUG_LOG("event: fd=%d mask=%d\n", fired[i].fd, fired[i].mask);

            if (fired[i].fd == listen_fd) {
                accept_clients(loop, listen_fd);
                continue;
            }

            if (clients[fired[i].fd] != NULL) {

                client* c = clients[fired[i].fd];

                if (!read_client(c, db)) {
                    close_client(loop, c);
                    c = NULL;
                }

                if (c == NULL) {
                    continue;
                }
            }
        }
    }

    for (i32 i = 0; i < MAX_CLIENTS; i++) {
        close_client(loop, clients[i]);
    }
    el_free(loop);
    close(listen_fd);
    ht_free(db);

    return 1;
}
