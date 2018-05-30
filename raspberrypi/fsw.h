#pragma once

#include <stdbool.h>
#include <sys/select.h>

int fsw_init(void);
int fsw_register(int nfds, fd_set *rfds);
bool fsw_has_events(fd_set *rfds);
void fsw_poll(void);
