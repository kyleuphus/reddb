#include "server.h"
#include "aof.h"
#include "buffer.h"
#include "command.h"
#include "debug.h"
#include "el.h"
#include "hashtable.h"
#include "mstime.h"
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

static b8 flush_buf(el_loop* l, client* c) {

    while (c->out_sent < c->out.len) {
        ssize accepted =
            send(c->fd, c->out.data + c->out_sent, c->out.len - c->out_sent, 0);
        if (accepted > 0) {
            c->out_sent += (usize)accepted;
        } else if (accepted == -1) {
            if (errno == EINTR) {
                continue;
            } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
                el_update(l, c->fd, c->mask, c->mask | EL_WRITABLE);
                c->mask = c->mask | EL_WRITABLE;
                return true;
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    c->out.len = 0;
    c->out_sent = 0;
    el_update(l, c->fd, c->mask, c->mask & ~EL_WRITABLE);
    c->mask = c->mask & ~EL_WRITABLE;
    return (!c->close_after_reply);
}

static b8 read_client(el_loop* l, client* c, ht* db, buffer* aof_buf,
                      i32 log_fd) {

    char scratch[1024 * 16];
    ssize n;

    n = recv(c->fd, scratch, sizeof(scratch), 0);

    if (n > 0) {
        if (!buf_append(&c->in, scratch, (usize)n)) {
            return false;
        }

        parse_status status = client_process_input(c, db, aof_buf);
        if (c->out.oom) {
            return false;
        }

        if (status == PARSE_NOMEM || status == PARSE_INVALID) {
            c->close_after_reply = true;
            el_update(l, c->fd, c->mask, c->mask & ~EL_READABLE);
            c->mask = c->mask & ~EL_READABLE;
        }

        if (c->out.len > 0) {
            if (aof_buf->oom ||
                !aof_write_all(log_fd, aof_buf->data, aof_buf->len)) {
                perror("aof write");
                exit(1);
            }
            if (!flush_buf(l, c)) {
                return false;
            }
        }

        aof_buf->len = 0;
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
    c->close_after_reply = false;
}

parse_status client_process_input(client* c, ht* db, buffer* aof_buf) {

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
            command_dispatch(r.argv, r.arglen, r.argc, &c->out, db, mstime(),
                             aof_buf);
            buf_consume(&c->in, r.bytes_consumed);
        }
        parse_result_free(&r);
    }
    return status;
}

int server_run(u16 port) {

    ht* db = ht_create();

    if (!aof_load("appendonly.aof", db)) {
        ht_free(db);
        return 1;
    }

    i32 log_fd = aof_open("appendonly.aof");
    if (log_fd < 0) {
        perror("log");
        ht_free(db);
        return 1;
    }

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

    el_loop* loop = el_create();
    if (loop == NULL) {
        perror("loop");
        return 1;
    }
    el_update(loop, listen_fd, EL_NONE, EL_READABLE);
    el_fired fired[EL_MAX_EVENTS];

    i64 next_pass = mstime();
    i64 next_fsync = mstime() + 1000;

    buffer aof_buf;
    buf_init(&aof_buf);
    for (;;) {

        i32 n = el_poll(loop, fired, 100);

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

                if (fired[i].mask & EL_READABLE && c->mask & EL_READABLE) {
                    if (!read_client(loop, c, db, &aof_buf, log_fd)) {
                        close_client(loop, c);
                        c = NULL;
                    }
                }

                if (c == NULL) {
                    continue;
                }

                if (fired[i].mask & EL_WRITABLE && c->mask & EL_WRITABLE) {
                    if (!flush_buf(loop, c)) {
                        close_client(loop, c);
                    }
                }
            }
        }
        i64 now = mstime();
        if (now >= next_pass) {
            ht_active_expire(db, 1000, now);
            next_pass = now + 100;
        }
        if (now >= next_fsync) {
            if (!aof_fsync(log_fd)) {
                perror("fsync");
                return 1;
            }
            next_fsync = now + 1000;
        }
    }

    for (i32 i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] != NULL) {
            close_client(loop, clients[i]);
        }
    }
    el_free(loop);
    close(listen_fd);
    ht_free(db);

    return 1;
}
