#include "buffer.h"
#include "command.h"
#include "parser.h"
#include "resp.h"
#include <stdio.h>
#include <string.h>

int main(void) {

    buffer_t b;
    buf_init(&b);

    resp_write_null(&b);

    printf("%.*s", (int)b.len, b.data);

    return 0;
}
