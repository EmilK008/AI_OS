/* ===========================================================================
 * App Menu - Popup launcher from AI_OS taskbar button
 * =========================================================================== */
#include "appmenu.h"
#include "framebuffer.h"
#include "terminal.h"
#include "snake_window.h"
#include "filemgr.h"
#include "shell.h"
#include "process.h"

#define MENU_X       2
#define MENU_ITEM_W  120
#define MENU_ITEM_H  22
#define MENU_ITEMS   3
#define MENU_PAD     2

static bool menu_open = false;

static const char *menu_labels[MENU_ITEMS] = {
    "Terminal",
    "Snake",
    "Files",
};

static int menu_y(void) {
    return fb_get_height() - 28 - (MENU_ITEMS * MENU_ITEM_H) - MENU_PAD;
}

void appmenu_toggle(void) {
    menu_open = !menu_open;
}

void appmenu_close(void) {
    menu_open = false;
}

bool appmenu_is_open(void) {
    return menu_open;
}

int appmenu_hit_test(int mx, int my) {
    if (!menu_open) return -1;
    int y0 = menu_y();
    if (mx < MENU_X || mx >= MENU_X + MENU_ITEM_W) return -1;
    if (my < y0 || my >= y0 + MENU_ITEMS * MENU_ITEM_H) return -1;
    return (my - y0) / MENU_ITEM_H;
}

void appmenu_on_click(int item) {
    menu_open = false;
    switch (item) {
    case 0: /* Terminal */
        if (!terminal_is_alive()) {
            terminal_reopen();
            process_create("shell", shell_entry);
        }
        break;
    case 1: /* Snake */
        snake_window_create();
        break;
    case 2: /* Files */
        filemgr_create();
        break;
    }
}

void appmenu_render(void) {
    if (!menu_open) return;

    int y0 = menu_y();

    /* Background + border */
    fb_fill_rect(MENU_X, y0, MENU_ITEM_W, MENU_ITEMS * MENU_ITEM_H, COLOR_RGB(60, 60, 60));
    fb_draw_rect(MENU_X, y0, MENU_ITEM_W, MENU_ITEMS * MENU_ITEM_H, COLOR_LIGHT_GREY);

    /* Items */
    for (int i = 0; i < MENU_ITEMS; i++) {
        int iy = y0 + i * MENU_ITEM_H;
        /* Separator line between items */
        if (i > 0)
            fb_fill_rect(MENU_X + 4, iy, MENU_ITEM_W - 8, 1, COLOR_RGB(80, 80, 80));
        fb_draw_string(MENU_X + 8, iy + 3, menu_labels[i], COLOR_WHITE, COLOR_RGB(60, 60, 60));
    }
}
