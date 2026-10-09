#define _POSIX_C_SOURCE 200809L
#include "aof.h"
#include "command.h"
#include "parser.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

i32 aof_open(const char* path) {
    return open(path, O_WRONLY | O_APPEND | O_CREAT, 0644);
}

b8 aof_write_all(i32 fd, const char* data, usize len) {
    usize done = 0;
    while (done < len) {
        ssize n = write(fd, data + done, len - done);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        done += (usize)n;
    }
    return true;
}

b8 aof_fsync(i32 fd) {
#ifdef F_FULLFSYNC
    return fcntl(fd, F_FULLFSYNC) != -1;
#else
    return fsync(fd) == 0;
#endif
}

void aof_append_command(buffer* b, const char** argv, const usize* arglen,
                        i32 argc) {

    char hdr[32];
    int n = snprintf(hdr, sizeof(hdr), "*%d\r\n", argc);
    buf_append(b, hdr, (usize)n);
    for (i32 i = 0; i < argc; i++) {
        n = snprintf(hdr, sizeof(hdr), "$%zu\r\n", arglen[i]);
        buf_append(b, hdr, (usize)n);
        buf_append(b, argv[i], arglen[i]);
        buf_append(b, "\r\n", 2);
    }
}

b8 aof_load(const char* path, ht* db) {
    errno = 0;
    i32 log_fd = open(path, O_RDONLY);
    if (log_fd == -1) {
        if (errno == ENOENT) {
            return true;
        } else {
            perror("read log");
            return false;
        }
    }

    usize good_bytes = 0;
    usize count = 0;

    buffer aof_in;
    buf_init(&aof_in);
    buffer aof_out;
    buf_init(&aof_out);
    ssize n = 1;

    while (n != 0) {

        char scratch[1024 * 16];
        errno = 0;
        n = read(log_fd, scratch, sizeof(scratch));

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            } else {
                perror("read log");
                buf_free(&aof_in);
                buf_free(&aof_out);
                close(log_fd);
                return false;
            }
        } else if (n > 0) {
            if (!buf_append(&aof_in, scratch, (usize)n)) {
                close(log_fd);
                buf_free(&aof_in);
                buf_free(&aof_out);
                return false;
            }

            parse_status status = PARSE_INCOMPLETE;
            for (;;) {
                parse_result r = parse_command(&aof_in);
                if (r.status == PARSE_INCOMPLETE) {
                    parse_result_free(&r);
                    break;
                } else if (r.status == PARSE_INVALID) {
                    parse_result_free(&r);
                    status = PARSE_INVALID;
                    fprintf(stderr, "corrupt log\n");
                    break;
                } else if (r.status == PARSE_NOMEM) {
                    parse_result_free(&r);
                    fprintf(stderr, "out of memory while replaying\n");
                    status = PARSE_NOMEM;
                    break;
                } else {
                    command_dispatch(r.argv, r.arglen, r.argc, &aof_out, db, 0,
                                     NULL);
                    if (aof_out.len > 0 && aof_out.data[0] == '-') {
                        fprintf(stderr, "replayed command failed %.*s\n",
                                (i32)aof_out.len, aof_out.data);
                        close(log_fd);
                        parse_result_free(&r);
                        buf_free(&aof_in);
                        buf_free(&aof_out);
                        return false;
                    }
                    aof_out.len = 0;
                    buf_consume(&aof_in, r.bytes_consumed);
                }
                count++;
                good_bytes += r.bytes_consumed;
                parse_result_free(&r);
            }
            if (aof_in.oom) {
                close(log_fd);
                fprintf(stderr, "ran out of memory on replay\n");
                buf_free(&aof_in);
                buf_free(&aof_out);
                return false;
            }

            if (status == PARSE_NOMEM || status == PARSE_INVALID) {
                close(log_fd);
                buf_free(&aof_in);
                buf_free(&aof_out);
                return false;
            }
        }
    }

    if (aof_in.len > 0) {
        if (truncate(path, good_bytes) == -1) {
            perror("truncate");
            close(log_fd);
            buf_free(&aof_in);
            buf_free(&aof_out);
            return false;
        }

        fprintf(stderr, "aof: cut %zu byte partial record at offset %zu\n",
                aof_in.len, good_bytes);
    }

    close(log_fd);
    buf_free(&aof_in);
    buf_free(&aof_out);
    fprintf(stderr, "aof: replayed %zu commands\n", count);
    return true;
}
