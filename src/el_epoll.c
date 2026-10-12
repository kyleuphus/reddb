#include "el.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/epoll.h>

struct el_loop {
    i32 epoll_fd;
    struct epoll_event events[EL_MAX_EVENTS];
};

el_loop* el_create(void) {
    el_loop* l = malloc(sizeof(el_loop));
    if (l == NULL) {
        return NULL;
    }

    i32 fd = epoll_create1(0);
    if (fd == -1) {
        free(l);
        return NULL;
    }
    l->epoll_fd = fd;
    return l;
}
void el_free(el_loop* l) {
    if (l != NULL) {
        close(l->epoll_fd);
    }
    free(l);
}

b8 el_update(el_loop* l, i32 fd, i32 old_mask, i32 new_mask) {
    if (old_mask == new_mask) {
        return true;
    }

    u32 op;
    if (old_mask == EL_NONE) {
        op = EPOLL_CTL_ADD;
    } else if (new_mask == EL_NONE) {
        op = EPOLL_CTL_DEL;
    } else {
        op = EPOLL_CTL_MOD;
    }

    struct epoll_event ev = {0};

    ev.data.fd = fd;

    if (new_mask & EL_READABLE) {
        ev.events |= EPOLLIN;
    }

    if (new_mask & EL_WRITABLE) {
        ev.events |= EPOLLOUT;
    }

    if (epoll_ctl(l->epoll_fd, op, fd, &ev) == -1) {
        return false;
    }

    return true;
}

i32 el_poll(el_loop* l, el_fired fired[EL_MAX_EVENTS], i32 timeout_ms) {

    i32 n = epoll_wait(l->epoll_fd, l->events, EL_MAX_EVENTS, timeout_ms);

    if (n < 0) {
        if (errno == EINTR) {
            return 0;
        }
        return -1;
    }

    for (i32 i = 0; i < n; i++) {
        fired[i].fd = l->events[i].data.fd;
        fired[i].mask = EL_NONE;
        if (l->events[i].events & EPOLLIN) {
            fired[i].mask |= EL_READABLE;
        }
        if (l->events[i].events & EPOLLOUT) {
            fired[i].mask |= EL_WRITABLE;
        }
        // epoll reports ERR/HUP unrequested, so this forces it to hit
        // the error in recv/send and close the client
        if (l->events[i].events & (EPOLLERR | EPOLLHUP)) {
            fired[i].mask |= EL_WRITABLE | EL_READABLE;
        }
    }

    return n;
}
