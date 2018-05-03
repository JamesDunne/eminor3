#pragma once

#include <stdbool.h>

int ts_init(void);

void ts_shutdown(void);

bool ts_poll(void);