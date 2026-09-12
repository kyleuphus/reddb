#ifndef EL_H
#define EL_H

#include "types.h"

#define EL_NONE 0
#define EL_READABLE 1
#define EL_WRITABLE 2

#define EL_MAX_EVENTS 128

typedef struct el_loop el_loop; // opaque: each backend defines it

typedef struct {
    i32 fd;
    i32 mask; // EL_READABLE and/or EL_WRITABLE
} el_fired;

el_loop* el_create(void);
void el_free(el_loop* l);

b8 el_update(el_loop* l, i32 fd, i32 old_mask, i32 new_mask);

i32 el_poll(el_loop* l, el_fired fired[EL_MAX_EVENTS], i32 timeout_ms);

#endif
