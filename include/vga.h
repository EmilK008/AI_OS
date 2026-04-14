#ifndef VGA_H
#define VGA_H

#include "types.h"

/* VGA colors */
enum vga_color {
    VGA_BLACK        = 0,
    VGA_BLUE         = 1,
    VGA_GREEN        = 2,
    VGA_CYAN         = 3,
    VGA_RED          = 4,
    VGA_MAGENTA      = 5,
    VGA_BROWN        = 6,
    VGA_LIGHT_GREY   = 7,
    VGA_DARK_GREY    = 8,
    VGA_LIGHT_BLUE   = 9,
    VGA_LIGHT_GREEN  = 10,
    VGA_LIGHT_CYAN   = 11,
    VGA_LIGHT_RED    = 12,
    VGA_LIGHT_MAGENTA= 13,
    VGA_YELLOW       = 14,
    VGA_WHITE        = 15,
};

#define VGA_COLOR(fg, bg) ((uint8_t)((bg) << 4 | (fg)))

void vga_init(void);
void vga_clear(void);
void vga_set_color(uint8_t color);
void vga_putchar(char c);
void vga_print(const char *str);
void vga_print_colored(const char *str, uint8_t color);
void vga_print_dec(uint32_t n);
void vga_print_hex(uint32_t n);
int  vga_get_cursor_x(void);
int  vga_get_cursor_y(void);
void vga_putchar_at(int x, int y, char c, uint8_t color);
void vga_set_cursor_pos(int x, int y);
void vga_set_gui_mode(bool enabled);

#endif
