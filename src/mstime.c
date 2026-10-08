#include "mstime.h"
#include <time.h>

i64 mstime(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (i64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
