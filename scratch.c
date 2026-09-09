// #include "buffer.h"
// #include "command.h"
#include "hashtable.h"
// #include "parser.h"
// #include "resp.h"
#include <stdio.h>
#include <string.h>

int main(void) {

    ht* t = ht_create();
    const char* key = "kyle";
    const char* value = "sophomore";
    ht_set(t, key, strlen(key), value, strlen(value));

    usize* outlen;
    const char* rec = ht_get(t, key, strlen(key), outlen);

    printf("%.*s\n", (int)outlen, rec);

    key = "kyle";
    value = "junior";

    ht_set(t, key, strlen(key), value, strlen(value));

    rec = ht_get(t, key, strlen(key), outlen);
    printf("%.*s\n", (int)outlen, rec);

    return 0;
}
