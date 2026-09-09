#ifndef HASHTABLE_H
#define HASHTABLE_H

#include "types.h"

typedef struct ht ht;

ht* ht_create(void);

void ht_free(ht* t);

const char* ht_get(ht* t, const char* key, usize klen, usize* outlen);

b8 ht_set(ht* t, const char* key, usize klen, const char* value, usize vlen);

b8 ht_delete(ht* t, const char* key, usize klen);

#endif
