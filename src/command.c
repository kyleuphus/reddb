#include "command.h"
#include "resp.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void dispatch(char** argv, usize* arglen, i32 argc, buffer_t* out, ht* t) {

    if (argc == 0) {
        return;
    }

    if (strcasecmp(argv[0], "PING") == 0) {
        if (argc == 1) {
            resp_write_simple(out, "PONG");
        } else if (argc == 2) {
            resp_write_bulk(out, argv[1], arglen[1]);
        } else {
            resp_write_error(
                out, "ERR wrong number of arguments for 'ping' command");
        }
    } else if (strcasecmp(argv[0], "ECHO") == 0) {
        if (argc == 2) {
            resp_write_bulk(out, argv[1], arglen[1]);
        } else {
            resp_write_error(
                out, "ERR wrong number of arguments for 'echo' command");
        }
    } else if (strcasecmp(argv[0], "SET") == 0) {
        if (argc == 3) {
            if (ht_set(t, argv[1], arglen[1], argv[2], arglen[2])) {
                resp_write_simple(out, "OK");
            } else {
                resp_write_error(out, "ERR out of memory");
            }
        } else {
            resp_write_error(out,
                             "ERR wrong number of arguments for 'set' command");
        }
    } else if (strcasecmp(argv[0], "GET") == 0) {
        usize outlen;
        if (argc == 2) {
            const char* value = ht_get(t, argv[1], arglen[1], &outlen);
            if (value != NULL) {
                resp_write_bulk(out, value, outlen);
            } else {
                resp_write_null(out);
            }
        } else {
            resp_write_error(out,
                             "ERR wrong number of arguments for 'get' command");
        }
    } else if (strcasecmp(argv[0], "DEL") == 0) {
        if (argc == 2) {
            if (ht_delete(t, argv[1], arglen[1])) {
                resp_write_integer(out, 1);
            } else {
                resp_write_integer(out, 0);
            }
        } else {
            resp_write_error(out,
                             "ERR wrong number of arguments for 'del' command");
        }
    } else if (strcasecmp(argv[0], "EXISTS") == 0) {
        usize outlen;
        if (argc == 2) {
            if (ht_get(t, argv[1], arglen[1], &outlen) != NULL) {
                resp_write_integer(out, 1);
            } else {
                resp_write_integer(out, 0);
            }
        } else {
            resp_write_error(
                out, "ERR wrong number of arguments for 'exists' command");
        }

    } else if (strcasecmp(argv[0], "INCR") == 0) {
        usize outlen;
        if (argc == 2) {
            const char* value = ht_get(t, argv[1], arglen[1], &outlen);
            char tmp[32];

            if (value == NULL) {
                if (ht_set(t, argv[1], arglen[1], "1", 1)) {
                    resp_write_integer(out, 1);
                } else {
                    resp_write_error(out, "ERR out of memory");
                }
            } else if (outlen >= sizeof(tmp)) {
                resp_write_error(out,
                                 "ERR value is not an integer or out of range");
            } else {

                memcpy(tmp, value, outlen);
                tmp[outlen] = '\0';

                char* endptr;
                errno = 0;

                i64 ivalue = strtoll(tmp, &endptr, 10);
                usize i = (tmp[0] == '-') ? 1 : 0;
                b8 leading_ok = (tmp[i] >= '1' && tmp[i] <= '9');
                b8 is_zero = (outlen == 1 && tmp[0] == '0');

                if (endptr == tmp || endptr != tmp + outlen ||
                    errno == ERANGE) {
                    resp_write_error(
                        out, "ERR value is not an integer or out of range");
                } else if (ivalue >= LLONG_MAX) {
                    resp_write_error(
                        out, "ERR value is not an integer or out of range");
                } else if (outlen <= i || !(leading_ok || is_zero)) {
                    resp_write_error(
                        out, "ERR value is not an integer or out of range");
                } else {
                    ivalue++;
                    char int_str[32];
                    snprintf(int_str, sizeof(int_str), "%" PRId64, ivalue);
                    if (ht_set(t, argv[1], arglen[1], int_str,
                               strlen(int_str))) {
                        resp_write_integer(out, ivalue);
                    } else {
                        resp_write_error(out, "ERR out of memory");
                    }
                }
            }
        } else {
            resp_write_error(
                out, "ERR wrong number of arguments for 'incr' command");
        }
    } else {
        resp_write_error(out, "ERR unknown command");
    }
}
