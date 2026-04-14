#ifndef WINDOW_H
#define WINDOW_H

#include "types.h"
#include "framebuffer.h"
#include "event.h"

#define MAX_WINDOWS     16
#define TITLEBAR_HEIGHT 22
#define BORDER_WIDTH    2
#define CLOSE_BTN_SIZE  16

struct window {
    int       id;
    char      title[64];
    int16_t   x, y;
    uint16_t  width, height;       /* total size including decorations */
    uint16_t  content_w, content_h;
    bool      visible, focused, alive;
    color_t  *content;
    void    (*on_event)(struct window *win, struct gui_event *evt);
    void     *user_data;
};

void wm_init(void);
int  wm_create_window(const char *title, int x, int y, int content_w, int content_h,
                       void (*on_event)(struct window *, struct gui_event *), void *user_data);
void wm_destroy_window(int id);
struct window *wm_get_window(int id);
void wm_focus_window(int id);
void wm_render_all(void);
void wm_handle_event(struct gui_event *evt);

#endif
