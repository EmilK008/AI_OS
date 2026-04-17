/* ===========================================================================
 * Window Manager
 * Manages windows, z-order, compositing, dragging, and event dispatch
 * =========================================================================== */
#include "window.h"
#include "framebuffer.h"
#include "desktop.h"
#include "mouse.h"
#include "string.h"
#include "memory.h"
#include "terminal.h"
#include "shell.h"
#include "process.h"
#include "appmenu.h"
#include "keyboard.h"

static struct window windows[MAX_WINDOWS];
static int z_order[MAX_WINDOWS];
static int z_count = 0;

/* Drag state */
static bool     dragging = false;
static int      drag_id = -1;
static int16_t  drag_off_x, drag_off_y;

/* Alt+Tab switcher state */
static bool switcher_showing = false;
static int  switcher_sel = 0;

/* 12x19 arrow cursor bitmap: 0=transparent, 1=black, 2=white */
static const uint8_t cursor_data[19][12] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0},
    {1,2,2,2,2,2,2,2,2,1,0,0},
    {1,2,2,2,2,2,2,2,2,2,1,0},
    {1,2,2,2,2,2,2,1,1,1,1,1},
    {1,2,2,2,1,2,2,1,0,0,0,0},
    {1,2,2,1,0,1,2,2,1,0,0,0},
    {1,2,1,0,0,1,2,2,1,0,0,0},
    {1,1,0,0,0,0,1,2,2,1,0,0},
    {1,0,0,0,0,0,1,2,2,1,0,0},
    {0,0,0,0,0,0,0,1,2,1,0,0},
    {0,0,0,0,0,0,0,1,1,0,0,0},
};

void wm_init(void) {
    for (int i = 0; i < MAX_WINDOWS; i++) {
        windows[i].alive = false;
        windows[i].id = i;
    }
    z_count = 0;
    dragging = false;
}

int wm_create_window(const char *title, int x, int y, int content_w, int content_h,
                      void (*on_event)(struct window *, struct gui_event *), void *user_data) {
    /* Find free slot */
    int slot = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!windows[i].alive) { slot = i; break; }
    }
    if (slot < 0) return -1;

    struct window *win = &windows[slot];
    str_ncopy(win->title, title, 64);
    win->x = (int16_t)x;
    win->y = (int16_t)y;
    win->content_w = (uint16_t)content_w;
    win->content_h = (uint16_t)content_h;
    win->width  = (uint16_t)(content_w + 2 * BORDER_WIDTH);
    win->height = (uint16_t)(content_h + TITLEBAR_HEIGHT + BORDER_WIDTH);
    win->visible = true;
    win->focused = false;
    win->alive = true;
    win->on_event = on_event;
    win->user_data = user_data;

    /* Allocate content buffer */
    win->content = (color_t *)kmalloc((uint32_t)(content_w * content_h) * sizeof(color_t));
    if (!win->content) {
        win->alive = false;
        return -1;
    }
    /* Fill content with dark background */
    for (int i = 0; i < content_w * content_h; i++) {
        win->content[i] = 0x00111111;
    }

    /* Add to z-order (on top) */
    z_order[z_count] = slot;
    z_count++;

    /* Focus this window */
    wm_focus_window(slot);

    return slot;
}

void wm_destroy_window(int id) {
    if (id < 0 || id >= MAX_WINDOWS || !windows[id].alive) return;
    windows[id].alive = false;
    if (windows[id].content) {
        kfree(windows[id].content);
        windows[id].content = NULL;
    }

    /* Remove from z-order */
    int pos = -1;
    for (int i = 0; i < z_count; i++) {
        if (z_order[i] == id) { pos = i; break; }
    }
    if (pos >= 0) {
        for (int i = pos; i < z_count - 1; i++) {
            z_order[i] = z_order[i + 1];
        }
        z_count--;
    }

    /* Focus topmost remaining */
    if (z_count > 0) {
        wm_focus_window(z_order[z_count - 1]);
    }
}

struct window *wm_get_window(int id) {
    if (id < 0 || id >= MAX_WINDOWS || !windows[id].alive) return NULL;
    return &windows[id];
}

void wm_focus_window(int id) {
    if (id < 0 || id >= MAX_WINDOWS || !windows[id].alive) return;

    /* Unfocus all */
    for (int i = 0; i < MAX_WINDOWS; i++) {
        windows[i].focused = false;
    }
    windows[id].focused = true;

    /* Move to top of z-order */
    int pos = -1;
    for (int i = 0; i < z_count; i++) {
        if (z_order[i] == id) { pos = i; break; }
    }
    if (pos >= 0 && pos < z_count - 1) {
        for (int i = pos; i < z_count - 1; i++) {
            z_order[i] = z_order[i + 1];
        }
        z_order[z_count - 1] = id;
    }
}

/* Hit test: returns window id at (mx, my) or -1 */
static int wm_hit_test(int mx, int my) {
    /* Front to back */
    for (int i = z_count - 1; i >= 0; i--) {
        struct window *win = &windows[z_order[i]];
        if (!win->alive || !win->visible) continue;
        if (mx >= win->x && mx < win->x + win->width &&
            my >= win->y && my < win->y + win->height) {
            return z_order[i];
        }
    }
    return -1;
}

static bool in_title_bar(struct window *win, int mx, int my) {
    return (my >= win->y + BORDER_WIDTH && my < win->y + TITLEBAR_HEIGHT &&
            mx >= win->x && mx < win->x + win->width);
}

static bool in_close_button(struct window *win, int mx, int my) {
    int bx = win->x + win->width - BORDER_WIDTH - CLOSE_BTN_SIZE - 2;
    int by = win->y + BORDER_WIDTH + (TITLEBAR_HEIGHT - BORDER_WIDTH - CLOSE_BTN_SIZE) / 2;
    return (mx >= bx && mx < bx + CLOSE_BTN_SIZE &&
            my >= by && my < by + CLOSE_BTN_SIZE);
}

static bool in_content(struct window *win, int mx, int my) {
    int cx = win->x + BORDER_WIDTH;
    int cy = win->y + TITLEBAR_HEIGHT;
    return (mx >= cx && mx < cx + win->content_w &&
            my >= cy && my < cy + win->content_h);
}

void wm_handle_event(struct gui_event *evt) {
    /* Block input while Alt+Tab switcher is active */
    if (switcher_showing) return;

    if (evt->type == EVT_MOUSE_DOWN && evt->mouse_button == 0) {
        /* Check if app menu item clicked */
        if (appmenu_is_open()) {
            int item = appmenu_hit_test(evt->mouse_x, evt->mouse_y);
            if (item >= 0) {
                appmenu_on_click(item);
                return;
            }
            /* Click outside menu → close it */
            appmenu_close();
            /* Fall through to allow clicking on windows */
        }

        /* Check if AI_OS button clicked (taskbar, x:2-56, bottom 26px) */
        int h = fb_get_height();
        if (evt->mouse_x >= 2 && evt->mouse_x <= 56 &&
            evt->mouse_y >= h - 26 && evt->mouse_y <= h - 4) {
            appmenu_toggle();
            return;
        }

        int id = wm_hit_test(evt->mouse_x, evt->mouse_y);
        if (id >= 0) {
            struct window *win = &windows[id];
            wm_focus_window(id);

            if (in_close_button(win, evt->mouse_x, evt->mouse_y)) {
                wm_destroy_window(id);
                return;
            }

            if (in_title_bar(win, evt->mouse_x, evt->mouse_y)) {
                dragging = true;
                drag_id = id;
                drag_off_x = (int16_t)(evt->mouse_x - win->x);
                drag_off_y = (int16_t)(evt->mouse_y - win->y);
                return;
            }

            if (in_content(win, evt->mouse_x, evt->mouse_y) && win->on_event) {
                win->on_event(win, evt);
            }
        }
    } else if (evt->type == EVT_MOUSE_UP && evt->mouse_button == 0) {
        if (dragging) {
            dragging = false;
            drag_id = -1;
        }
        /* Forward mouse up to focused window */
        for (int i = z_count - 1; i >= 0; i--) {
            struct window *win = &windows[z_order[i]];
            if (win->alive && win->focused && win->on_event) {
                win->on_event(win, evt);
                break;
            }
        }
    } else if (evt->type == EVT_MOUSE_MOVE) {
        if (dragging && drag_id >= 0 && windows[drag_id].alive) {
            struct window *win = &windows[drag_id];
            win->x = (int16_t)(evt->mouse_x - drag_off_x);
            win->y = (int16_t)(evt->mouse_y - drag_off_y);
            /* Clamp so title bar stays on screen */
            if (win->y < 0) win->y = 0;
            if (win->y > fb_get_height() - TITLEBAR_HEIGHT)
                win->y = (int16_t)(fb_get_height() - TITLEBAR_HEIGHT);
        } else {
            /* Forward mouse move to focused window */
            for (int i = z_count - 1; i >= 0; i--) {
                struct window *win = &windows[z_order[i]];
                if (win->alive && win->focused && win->on_event) {
                    win->on_event(win, evt);
                    break;
                }
            }
        }
    } else if (evt->type == EVT_KEY_PRESS) {
        /* Route to focused window */
        for (int i = z_count - 1; i >= 0; i--) {
            struct window *win = &windows[z_order[i]];
            if (win->alive && win->focused && win->on_event) {
                win->on_event(win, evt);
                break;
            }
        }
    }
}

/* ---- Alt+Tab Window Switcher ---- */

void wm_alt_tab(void) {
    if (z_count < 2) return;
    if (!switcher_showing) {
        switcher_showing = true;
        /* Start at the window behind the current top */
        switcher_sel = z_count - 2;
    } else {
        /* Cycle backwards through z-order */
        switcher_sel--;
        if (switcher_sel < 0) switcher_sel = z_count - 1;
    }
}

bool wm_switcher_active(void) {
    return switcher_showing;
}

void wm_switcher_commit(void) {
    if (!switcher_showing) return;
    switcher_showing = false;
    if (switcher_sel >= 0 && switcher_sel < z_count) {
        wm_focus_window(z_order[switcher_sel]);
    }
}

static void draw_switcher_overlay(void) {
    if (!switcher_showing || z_count == 0) return;

    int scr_w = fb_get_width();
    int scr_h = fb_get_height();

    /* Calculate overlay size */
    int item_w = 160;
    int item_h = 28;
    int padding = 8;
    int total_w = item_w + padding * 2;
    int total_h = z_count * item_h + padding * 2;
    int ox = (scr_w - total_w) / 2;
    int oy = (scr_h - total_h) / 2;

    /* Dark semi-transparent background (checkerboard for "transparency") */
    for (int y = oy - 2; y < oy + total_h + 2; y++) {
        for (int x = ox - 2; x < ox + total_w + 2; x++) {
            if ((x + y) % 2 == 0)
                fb_putpixel(x, y, 0x00000000);
        }
    }

    /* Solid background box */
    fb_fill_rect(ox, oy, total_w, total_h, 0x00202030);
    fb_draw_rect(ox, oy, total_w, total_h, 0x00606080);

    /* Draw each window entry */
    for (int i = 0; i < z_count; i++) {
        struct window *win = &windows[z_order[i]];
        if (!win->alive) continue;

        int ix = ox + padding;
        int iy = oy + padding + i * item_h;

        if (i == switcher_sel) {
            /* Highlight selected */
            fb_fill_rect(ix - 2, iy - 1, item_w + 4, item_h - 2, 0x00304878);
            fb_draw_rect(ix - 2, iy - 1, item_w + 4, item_h - 2, 0x005080C0);
            fb_draw_string(ix + 4, iy + 4, win->title, 0x00FFFFFF, 0x00304878);
        } else {
            fb_draw_string(ix + 4, iy + 4, win->title, 0x00BBBBBB, 0x00202030);
        }
    }
}

static void draw_window(struct window *win) {
    if (!win->alive || !win->visible) return;

    int x = win->x, y = win->y;
    int w = win->width, h = win->height;

    /* Shadow */
    fb_fill_rect(x + 3, y + 3, w, h, 0x00222222);

    /* Border */
    fb_fill_rect(x, y, w, h, COLOR_BORDER);

    /* Title bar */
    color_t title_color = win->focused ? COLOR_TITLE_ACTIVE : COLOR_TITLE_INACTIVE;
    fb_fill_rect(x + BORDER_WIDTH, y + BORDER_WIDTH,
                 w - 2 * BORDER_WIDTH, TITLEBAR_HEIGHT - BORDER_WIDTH,
                 title_color);

    /* Title text */
    fb_draw_string(x + BORDER_WIDTH + 6, y + BORDER_WIDTH + 3,
                   win->title, COLOR_WHITE, title_color);

    /* Close button */
    int bx = x + w - BORDER_WIDTH - CLOSE_BTN_SIZE - 2;
    int by = y + BORDER_WIDTH + (TITLEBAR_HEIGHT - BORDER_WIDTH - CLOSE_BTN_SIZE) / 2;
    fb_fill_rect(bx, by, CLOSE_BTN_SIZE, CLOSE_BTN_SIZE, COLOR_CLOSE_RED);
    /* Draw X */
    for (int i = 3; i < CLOSE_BTN_SIZE - 3; i++) {
        fb_putpixel(bx + i, by + i, COLOR_WHITE);
        fb_putpixel(bx + CLOSE_BTN_SIZE - 1 - i, by + i, COLOR_WHITE);
        fb_putpixel(bx + i + 1, by + i, COLOR_WHITE);
        fb_putpixel(bx + CLOSE_BTN_SIZE - i, by + i, COLOR_WHITE);
    }

    /* Content area */
    int cx = x + BORDER_WIDTH;
    int cy = y + TITLEBAR_HEIGHT;
    fb_blit(cx, cy, win->content_w, win->content_h, win->content, win->content_w);
}

static void draw_cursor(int mx, int my) {
    for (int y = 0; y < 19; y++) {
        for (int x = 0; x < 12; x++) {
            uint8_t val = cursor_data[y][x];
            if (val == 0) continue;
            color_t c = (val == 1) ? COLOR_BLACK : COLOR_WHITE;
            fb_putpixel(mx + x, my + y, c);
        }
    }
}

void wm_render_all(void) {
    /* 1. Desktop background */
    desktop_draw_background();

    /* 2. Windows back-to-front */
    for (int i = 0; i < z_count; i++) {
        draw_window(&windows[z_order[i]]);
    }

    /* 3. Taskbar (on top of windows) */
    desktop_draw_taskbar();

    /* Draw window buttons in taskbar */
    int btn_x = 60;
    for (int i = 0; i < z_count; i++) {
        struct window *win = &windows[z_order[i]];
        if (!win->alive || !win->visible) continue;
        color_t btn_color = win->focused ? COLOR_TITLE_ACTIVE : COLOR_BUTTON;
        fb_fill_rect(btn_x, fb_get_height() - 26, 120, 22, btn_color);
        fb_draw_rect(btn_x, fb_get_height() - 26, 120, 22, COLOR_LIGHT_GREY);
        fb_draw_string(btn_x + 4, fb_get_height() - 23, win->title, COLOR_WHITE, btn_color);
        btn_x += 124;
    }

    /* 4. App menu (above taskbar, below cursor) */
    appmenu_render();

    /* 5. Alt+Tab switcher overlay */
    draw_switcher_overlay();
    /* Auto-commit when Alt is released */
    if (switcher_showing && !keyboard_alt_held()) {
        wm_switcher_commit();
    }

    /* 6. Mouse cursor */
    draw_cursor(mouse_get_x(), mouse_get_y());

    /* 7. Flip */
    fb_flip();
}
