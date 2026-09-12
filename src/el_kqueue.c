#include "el.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/event.h>
#include <time.h>
#include <unistd.h>

struct el_loop {
    i32 kqueue_fd;
    struct kevent events[EL_MAX_EVENTS];
};

el_loop* el_create(void) {
    el_loop* l = malloc(sizeof(el_loop));
    if (l == NULL) {
        return NULL;
    }

    i32 fd = kqueue();
    if (fd == -1) {
        free(l);
        return NULL;
    }
    l->kqueue_fd = fd;
    return l;
}

void el_free(el_loop* l) {
    if (l != NULL) {
        close(l->kqueue_fd);
    }
    free(l);
}

b8 el_update(el_loop* l, i32 fd, i32 old_mask, i32 new_mask) {
    struct kevent changes[2];
    i32 n = 0;

    i32 added = new_mask & ~old_mask;
    i32 removed = old_mask & ~new_mask;

    if (added & EL_READABLE) {
        EV_SET(&changes[n++], fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    }
    if (added & EL_WRITABLE) {
        EV_SET(&changes[n++], fd, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
    }
    if (removed & EL_READABLE) {
        EV_SET(&changes[n++], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    }
    if (removed & EL_WRITABLE) {
        EV_SET(&changes[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    }
    if (n == 0) {
        return true;
    }

    return kevent(l->kqueue_fd, changes, n, NULL, 0, NULL) != -1;
}

i32 el_poll(el_loop* l, el_fired fired[EL_MAX_EVENTS], i32 timeout_ms) {

    struct timespec ts;
    struct timespec* tsp = NULL;

    if (timeout_ms >= 0) {
        ts.tv_sec = timeout_ms / 1000;
        ts.tv_nsec = timeout_ms % 1000 * 1000000;
        tsp = &ts;
    }

    i32 n = kevent(l->kqueue_fd, NULL, 0, l->events, EL_MAX_EVENTS, tsp);

    if (n < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return -1;
    }

    for (i32 i = 0; i < n; i++) {
        fired[i].fd = (i32)l->events[i].ident;
        fired[i].mask =
            (l->events[i].filter == EVFILT_READ) ? EL_READABLE : EL_WRITABLE;
    }

    return n;
}
