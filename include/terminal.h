#ifndef TERMINAL_H
#define TERMINAL_H

#include "types.h"
#include "event.h"
#include "window.h"

#define TERM_COLS 78
#define TERM_ROWS 24

void terminal_create(void);
void terminal_render(void);
void terminal_on_event(struct window *win, struct gui_event *evt);

/* Called by VGA redirect */
void terminal_putchar_redirect(char c);
void terminal_clear_redirect(void);
void terminal_set_color_redirect(uint8_t color);

#endif
