#ifndef HASHTABLE_H
#define HASHTABLE_H

#include "types.h"

typedef struct ht ht;

ht* ht_create(void);

void ht_free(ht* t);

const char* ht_get(ht* t, const char* key, usize klen, usize* outlen, i64 now);

b8 ht_set(ht* t, const char* key, usize klen, const char* value, usize vlen,
          i64 expire_at);

b8 ht_delete(ht* t, const char* key, usize klen, i64 now);

b8 ht_set_expire_at(ht* t, const char* key, usize klen, i64 ttl, i64 now);

b8 ht_set_expire_absolute(ht* t, const char* key, usize klen, i64 expire_at,
                          i64 now);

b8 ht_read_expire_at(ht* t, const char* key, usize klen, i64* ttl, i64 now);

usize ht_get_len(ht* t);

void ht_active_expire(ht* t, usize buckets, i64 now);

#endif
