#include "hashtable.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAPACITY 4
#define FNV_OFFSET 14695981039346656037UL
#define FNV_PRIME 1099511628211UL

typedef struct node {
    char* key;
    usize klen;
    char* value;
    usize vlen;
    i64 expire_at;
    struct node* next;
} node;

struct ht {
    usize capacity;
    usize length;
    node** buckets;
};

static u64 ht_hash(const char* key, usize klen) {

    u64 hash = FNV_OFFSET;
    const char* p = key;
    for (usize i = 0; i < klen; i++) {
        hash ^= (u64)(uchar)(*p);
        hash *= FNV_PRIME;
        p++;
    }

    return hash;
}

static b8 ht_delete_node(ht* t, const char* key, usize klen) {

    u64 hash = ht_hash(key, klen);
    usize index = (usize)(hash & (u64)(t->capacity - 1));

    if (t->buckets[index] == NULL) {
        return false;
    }

    node* n = t->buckets[index];
    node* previous;
    i32 count = 0;

    for (;;) {
        if (n->klen == klen && memcmp(key, n->key, klen) == 0) {
            if (count != 0) {
                previous->next = n->next;
            } else if (count == 0) {
                t->buckets[index] = n->next;
            }
            free(n->key);
            free(n->value);
            free(n);
            t->length--;
            return true;
        } else if (n->next == NULL) {
            return false;
        } else {
            previous = n;
            n = n->next;
            count++;
        }
    }
}

static node* ht_find_node(ht* t, const char* key, usize klen, i64 now) {

    u64 hash = ht_hash(key, klen);
    usize index = (usize)(hash & (u64)(t->capacity - 1));

    if (t->buckets[index] == NULL) {
        return NULL;
    }

    node* n = t->buckets[index];
    for (;;) {
        if (n->klen == klen && memcmp(key, n->key, klen) == 0) {
            b8 dead = n->expire_at != 0 && n->expire_at <= now;
            if (!dead) {
                return n;
            } else {
                ht_delete_node(t, key, klen);
                return NULL;
            }
        } else if (n->next == NULL) {
            return NULL;
        } else {
            n = n->next;
        }
    }
}

ht* ht_create(void) {

    ht* t = malloc(sizeof(*t));

    if (t == NULL) {
        return NULL;
    }

    t->capacity = INITIAL_CAPACITY;
    t->length = 0;

    t->buckets = malloc(t->capacity * sizeof(*t->buckets));

    if (t->buckets == NULL) {
        free(t);
        return NULL;
    }

    for (usize i = 0; i < t->capacity; i++) {
        t->buckets[i] = NULL;
    }

    return t;
}

void ht_free(ht* t) {

    if (t == NULL) {
        return;
    }

    for (usize i = 0; i < t->capacity; i++) {
        node* current = t->buckets[i];

        while (current != NULL) {
            node* next = current->next;

            free(current->key);
            free(current->value);
            free(current);
            current = next;
        }
    }

    free(t->buckets);
    free(t);
}

static b8 ht_bucket_insert(node** buckets, usize cap, const char* key,
                           usize klen, const char* value, usize vlen,
                           i64 expire_at, b8* inserted) {

    u64 hash = ht_hash(key, klen);
    usize index = (usize)(hash & (u64)(cap - 1));
    node* n = buckets[index];

    for (;;) {

        if (n == NULL) {

            node* new = malloc(sizeof(node));

            if (new == NULL) {
                return false;
            }

            char* key_copy = malloc(klen + 1);
            if (key_copy == NULL) {
                free(new);
                return false;
            }

            char* value_copy = malloc(vlen + 1);
            if (value_copy == NULL) {
                free(key_copy);
                free(new);
                return false;
            }

            memcpy(key_copy, key, klen);
            key_copy[klen] = '\0';

            memcpy(value_copy, value, vlen);
            value_copy[vlen] = '\0';

            new->key = key_copy;
            new->klen = klen;
            new->value = value_copy;
            new->vlen = vlen;
            new->next = buckets[index];
            new->expire_at = expire_at;

            buckets[index] = new;

            *inserted = true;

            return true;

        } else if (n->klen == klen && memcmp(key, n->key, klen) == 0) {

            char* value_copy = malloc(vlen + 1);

            if (value_copy == NULL) {
                return false;
            }

            memcpy(value_copy, value, vlen);
            value_copy[vlen] = '\0';

            free(n->value);

            n->value = value_copy;
            n->vlen = vlen;
            n->expire_at = expire_at;

            *inserted = false;

            return true;
        } else {
            n = n->next;
        }
    }
}

static b8 ht_grow(ht* t) {

    node** new_buckets = malloc(t->capacity * sizeof(*t->buckets) * 2);

    if (new_buckets == NULL) {
        return false;
    }

    for (usize i = 0; i < t->capacity * 2; i++) {
        new_buckets[i] = NULL;
    }

    usize new_cap = t->capacity * 2;

    for (usize i = 0; i < t->capacity; i++) {
        node* current = t->buckets[i];

        while (current != NULL) {
            node* next = current->next;
            usize index = ht_hash(current->key, current->klen) & (new_cap - 1);
            current->next = new_buckets[index];
            new_buckets[index] = current;
            current = next;
        }
    }

    free(t->buckets);
    t->buckets = new_buckets;
    t->capacity *= 2;

    return true;
}

const char* ht_get(ht* t, const char* key, usize klen, usize* outlen, i64 now) {

    node* n = ht_find_node(t, key, klen, now);

    if (n == NULL) {
        *outlen = 0;
        return NULL;
    } else {
        *outlen = n->vlen;
        return n->value;
    }
}

b8 ht_set(ht* t, const char* key, usize klen, const char* value, usize vlen,
          i64 expire_at) {

    if (value == NULL) {
        return false;
    }

    if (t->length >= t->capacity / 2) {
        if (!ht_grow(t)) {
            return false;
        }
    }

    b8 inserted;

    if (expire_at == -1) { // -1 requires the key to exist
        u64 hash = ht_hash(key, klen);
        usize index = (usize)(hash & (u64)(t->capacity - 1));
        node* n = t->buckets[index];
        b8 found = false;

        while (!found) {
            if (n->klen == klen && memcmp(key, n->key, klen) == 0) {
                expire_at = n->expire_at;
                found = true;
            } else {
                n = n->next;
            }
        }
        if (!ht_bucket_insert(t->buckets, t->capacity, key, klen, value, vlen,
                              expire_at, &inserted)) {
            return false;
        }
    } else {
        if (!ht_bucket_insert(t->buckets, t->capacity, key, klen, value, vlen,
                              expire_at, &inserted)) {
            return false;
        }
    }

    if (inserted) {
        t->length++;
    }

    return true;
}

b8 ht_delete(ht* t, const char* key, usize klen, i64 now) {

    node* n = ht_find_node(t, key, klen, now);

    if (n == NULL) {
        return false;
    } else {
        return ht_delete_node(t, key, klen);
    }
}

b8 ht_set_expire_at(ht* t, const char* key, usize klen, i64 ttl, i64 now) {

    node* n = ht_find_node(t, key, klen, now);

    if (n == NULL) {
        return false;
    } else {
        n->expire_at = now + ttl * 1000;
        return true;
    }
}

b8 ht_read_expire_at(ht* t, const char* key, usize klen, i64* ttl, i64 now) {

    node* n = ht_find_node(t, key, klen, now);

    if (n == NULL) {
        *ttl = -2;
        return false;
    } else {
        if (n->expire_at == 0) {
            *ttl = -1;
        } else {
            *ttl = (n->expire_at - now + 500) / 1000;
        }
        return true;
    }
}

usize ht_get_len(ht* t) { return t->length; }
