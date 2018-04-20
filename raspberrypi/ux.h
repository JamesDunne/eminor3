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
