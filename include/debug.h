#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

#ifdef REDDB_DEBUG
#define REDDB_DEBUG_ON 1
#else
#define REDDB_DEBUG_ON 0
#endif

#define DEBUG_LOG(...)                                                         \
    do {                                                                       \
        if (REDDB_DEBUG_ON) {                                                  \
            fprintf(stderr, __VA_ARGS__);                                      \
        }                                                                      \
    } while (0)

#endif
