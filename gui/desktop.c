/* ===========================================================================
 * Desktop - Background and Taskbar
 * =========================================================================== */
#include "desktop.h"
#include "framebuffer.h"
#include "timer.h"
#include "string.h"

void desktop_init(void) {
    /* Nothing to allocate yet */
}

void desktop_draw_background(void) {
    fb_clear(COLOR_DESKTOP_BG);
}

void desktop_draw_taskbar(void) {
    int h = fb_get_height();
    int w = fb_get_width();

    /* Taskbar: 28px at bottom */
    fb_fill_rect(0, h - 28, w, 28, COLOR_TASKBAR);
    fb_fill_rect(0, h - 28, w, 1, COLOR_LIGHT_GREY);

    /* AI_OS button */
    fb_fill_rect(2, h - 26, 54, 22, COLOR_BUTTON);
    fb_draw_rect(2, h - 26, 54, 22, COLOR_LIGHT_GREY);
    fb_draw_string(8, h - 23, "AI_OS", COLOR_CYAN, COLOR_BUTTON);

    /* Clock in right corner */
    uint32_t ticks = timer_get_ticks();
    uint32_t secs  = ticks / 100;
    uint32_t mins  = (secs / 60) % 60;
    uint32_t hrs   = (secs / 3600) % 24;
    secs %= 60;

    char time_str[9];
    time_str[0] = '0' + (hrs / 10);
    time_str[1] = '0' + (hrs % 10);
    time_str[2] = ':';
    time_str[3] = '0' + (mins / 10);
    time_str[4] = '0' + (mins % 10);
    time_str[5] = ':';
    time_str[6] = '0' + (secs / 10);
    time_str[7] = '0' + (secs % 10);
    time_str[8] = '\0';

    fb_draw_string(w - 72, h - 23, time_str, COLOR_WHITE, COLOR_TASKBAR);
}
