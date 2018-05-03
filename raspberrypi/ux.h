#pragma once

#include <stdbool.h>

// Initialize UX (user experience):
int ux_init(void);

// Mark UX as ready for redraw:
void ux_notify_redraw(void);

// Draw UX screen:
void ux_draw(void);

// Poll for UX events:
bool ux_poll(void);

void ux_ts_update_extents(int x_min, int x_max, int y_min, int y_max);
void ux_ts_update_row(int y);
void ux_ts_update_col(int x);
void ux_ts_update_touching(bool touching);
