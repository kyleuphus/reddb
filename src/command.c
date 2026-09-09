#include "command.h"
#include "resp.h"

#include <string.h>
#include <strings.h>

void dispatch(char** argv, usize* arglen, i32 argc, buffer_t* out) {

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
    } else {
        resp_write_error(out, "ERR unknown command");
    }
}
