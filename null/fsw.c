#include <sys/select.h>
#include "types.h"
#include "hardware.h"

int fsw_init(void) {
    return 0;
}

int fsw_register(int nfds, fd_set *rfds) {
    (void) rfds;
    return nfds;
}

bool fsw_has_events(fd_set *rfds) {
    return false;
}

void fsw_poll(void) {
    //if (is_btn_pressed(M_1)) {
    //    prev_song();
    //}
    //if (is_btn_pressed(M_2)) {
    //    next_song();
    //}
    //if (is_btn_pressed(M_3)) {
    //    next_scene();
    //}

    return;
}
