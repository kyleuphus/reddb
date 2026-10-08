#include "command.h"
#include "resp.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static b8 extract_int(const char* value, usize len, i64* out) {

    char tmp[32];
    if (len >= sizeof(tmp)) {
        return false;
    }
    memcpy(tmp, value, len);
    tmp[len] = '\0';

    char* endptr;
    errno = 0;

    *out = strtoll(tmp, &endptr, 10);
    usize i = (tmp[0] == '-') ? 1 : 0;
    b8 leading_ok = (tmp[i] >= '1' && tmp[i] <= '9');
    b8 is_zero = (len == 1 && tmp[0] == '0');

    if (endptr != tmp + len || errno == ERANGE || !(leading_ok || is_zero)) {
        return false;
    } else {
        return true;
    }
}

void command_dispatch(char** argv, usize* arglen, i32 argc, buffer* out, ht* db,
                      i64 now) {

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
            if (ht_set(db, argv[1], arglen[1], argv[2], arglen[2], 0)) {
                resp_write_simple(out, "OK");
            } else {
                resp_write_error(out, "ERR out of memory");
            }
        } else if (argc == 5) {
            if (strcasecmp(argv[3], "EX") == 0) {
                if (arglen[4] < 32) {
                    i64 ttl;
                    if (!extract_int(argv[4], arglen[4], &ttl)) {
                        resp_write_error(
                            out, "ERR value is not an integer or out of range");
                    } else if (ttl <= 0 || ttl > (INT64_MAX - now) / 1000) {
                        resp_write_error(
                            out, "ERR invalid expire time in \'set\' command");
                    } else {
                        if (ht_set(db, argv[1], arglen[1], argv[2], arglen[2],
                                   now + ttl * 1000)) {
                            resp_write_simple(out, "OK");
                        } else {
                            resp_write_error(out, "ERR out of memory");
                        }
                    }
                } else {
                    resp_write_error(
                        out, "ERR value is not an integer or out of range");
                }
            } else {
                resp_write_error(out, "ERR syntax error");
            }
        } else if (argc < 3) {
            resp_write_error(
                out, "ERR wrong number of arguments for \'set\' command");
        } else {
            resp_write_error(out, "ERR syntax error");
        }
    } else if (strcasecmp(argv[0], "GET") == 0) {
        usize outlen;
        if (argc == 2) {
            const char* value = ht_get(db, argv[1], arglen[1], &outlen, now);
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
            if (ht_delete(db, argv[1], arglen[1], now)) {
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
            if (ht_get(db, argv[1], arglen[1], &outlen, now) != NULL) {
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
            const char* value = ht_get(db, argv[1], arglen[1], &outlen, now);

            if (value == NULL) {
                if (ht_set(db, argv[1], arglen[1], "1", 1, 0)) {
                    resp_write_integer(out, 1);
                } else {
                    resp_write_error(out, "ERR out of memory");
                }
            } else {
                i64 ivalue;
                if (!extract_int(value, outlen, &ivalue)) {
                    resp_write_error(
                        out, "ERR value is not an integer or out of range");
                } else if (ivalue >= LLONG_MAX) {
                    resp_write_error(
                        out, "ERR value is not an integer or out of range");
                } else {
                    ivalue++;
                    char int_str[32];
                    snprintf(int_str, sizeof(int_str), "%" PRId64, ivalue);
                    if (ht_set(db, argv[1], arglen[1], int_str, strlen(int_str),
                               -1)) {
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
    } else if (strcasecmp(argv[0], "EXPIRE") == 0) {
        if (argc != 3) {
            resp_write_error(
                out, "ERR wrong number of arguments for 'expire' command");
        } else {
            i64 ttl;
            if (!extract_int(argv[2], arglen[2], &ttl)) {
                resp_write_error(out,
                                 "ERR value is not an integer or out of range");
            } else if (ttl <= 0) {
                if (ht_delete(db, argv[1], arglen[1], now)) {
                    resp_write_integer(out, 1);
                } else {
                    resp_write_integer(out, 0);
                }
            } else if (ttl > (INT64_MAX - now) / 1000) {
                resp_write_error(out,
                                 "ERR invalid expire time in 'expire' command");
            } else {
                if (!ht_set_expire_at(db, argv[1], arglen[1], ttl, now)) {
                    resp_write_integer(out, 0);
                } else {
                    resp_write_integer(out, 1);
                }
            }
        }
    } else if (strcasecmp(argv[0], "TTL") == 0) {
        if (argc != 2) {
            resp_write_error(out,
                             "ERR wrong number of arguments for 'ttl' command");
        } else {
            i64 ttl;
            if (!ht_read_expire_at(db, argv[1], arglen[1], &ttl, now)) {
                resp_write_integer(out, -2);
            } else {
                resp_write_integer(out, ttl);
            }
        }
    } else if (strcasecmp(argv[0], "DBSIZE") == 0) {
        if (argc != 1) {
            resp_write_error(
                out, "ERR wrong number of arguments for 'dbsize' command");
        } else {
            resp_write_integer(out, ht_get_len(db));
        }
    } else {
        resp_write_error(out, "ERR unknown command");
    }
}
