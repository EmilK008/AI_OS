#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "types.h"

typedef uint32_t color_t;

#define COLOR_RGB(r,g,b) ((color_t)(((r)<<16)|((g)<<8)|(b)))

#define COLOR_BLACK        0x00000000
#define COLOR_WHITE        0x00FFFFFF
#define COLOR_DESKTOP_BG   0x00008080
#define COLOR_TITLE_ACTIVE 0x000055AA
#define COLOR_TITLE_INACTIVE 0x00808080
#define COLOR_DARK_GREY    0x00404040
#define COLOR_LIGHT_GREY   0x00C0C0C0
#define COLOR_RED          0x00CC0000
#define COLOR_TASKBAR      0x00333333
#define COLOR_BUTTON       0x00555555
#define COLOR_BUTTON_HOVER 0x00666666
#define COLOR_CLOSE_RED    0x00DD3333
#define COLOR_GREEN        0x0000AA00
#define COLOR_YELLOW       0x00CCCC00
#define COLOR_CYAN         0x0000AAAA
#define COLOR_BORDER       0x00222222

void    fb_init(void);
bool    fb_available(void);
void    fb_putpixel(int x, int y, color_t color);
void    fb_fill_rect(int x, int y, int w, int h, color_t color);
void    fb_draw_rect(int x, int y, int w, int h, color_t color);
void    fb_draw_char(int x, int y, char c, color_t fg, color_t bg);
void    fb_draw_char_transparent(int x, int y, char c, color_t fg);
void    fb_draw_string(int x, int y, const char *str, color_t fg, color_t bg);
void    fb_blit(int dst_x, int dst_y, int w, int h, const color_t *src, int src_pitch);
void    fb_flip(void);
void    fb_clear(color_t color);
int     fb_get_width(void);
int     fb_get_height(void);
color_t *fb_get_backbuf(void);
bool    fb_set_mode(int width, int height);

#endif
