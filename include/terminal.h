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
void terminal_putchar_at_redirect(int x, int y, char c, uint8_t color);
void terminal_clear_redirect(void);
void terminal_set_color_redirect(uint8_t color);
void terminal_set_cursor_redirect(int x, int y);

/* Terminal lifecycle */
bool terminal_is_alive(void);
void terminal_reopen(void);

#endif
