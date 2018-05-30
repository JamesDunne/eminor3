#pragma once

#include <stdbool.h>
#include <sys/select.h>

int ts_init(void);
void ts_shutdown(void);
int ts_register(int nfds, fd_set *rfds);
bool ts_has_events(fd_set *rfds);
bool ts_poll(void);
