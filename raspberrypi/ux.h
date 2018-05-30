#pragma once

#include <stdbool.h>

// Initialize UX (user experience):
int ux_init(void);

// Wait for UX events:
void ux_select(void);

void ux_ts_update_extents(int x_min, int x_max, int y_min, int y_max);
void ux_ts_update_row(int y);
void ux_ts_update_col(int x);
void ux_ts_update_touching(bool touching);
