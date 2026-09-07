#include "buffer.h"
#include "command.h"
#include "parser.h"
#include "resp.h"
#include <stdio.h>
#include <string.h>

int main(void) {

    buffer_t in;
    buf_init(&in);

    buffer_t out;
    buf_init(&out);

    const char* temp = "*2\r\n$4\r\nECHO\r\n$5\r\nhello\r\n";
    buf_append(&in, temp, strlen(temp));
    ParseResult parsed = parse(&in);

    dispatch(parsed.argv, parsed.argc, &out);

    printf("array sent: %s\n", temp);
    printf("arg count: %d\n", parsed.argc);
    printf("args parsed: \n");
    for (int i = 0; i < parsed.argc; i++) {
        printf("%s\n", parsed.argv[i]);
    }
    printf("response written: %s\n", out.data);

    return 0;
}
