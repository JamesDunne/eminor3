#include <sys/select.h>
#include "types.h"
#include "hardware.h"

int ts_init(void) {
    return 0;
}

void ts_shutdown(void) {
}

int ts_register(int nfds, fd_set *rfds) {
    (void) rfds;
    return nfds;
}

bool ts_has_events(fd_set *rfds) {
    return false;
}

bool ts_poll(void) {
    return false;
}
