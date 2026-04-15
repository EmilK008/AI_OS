#ifndef DESKTOP_H
#define DESKTOP_H

#include "types.h"
#include "framebuffer.h"

void desktop_init(void);
void desktop_draw_background(void);
void desktop_draw_taskbar(void);
void desktop_set_bg(color_t c);
color_t desktop_get_bg(void);
void desktop_set_wallpaper(const char *filename);
void desktop_clear_wallpaper(void);
bool desktop_has_wallpaper(void);

#endif
