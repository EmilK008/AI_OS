/* ===========================================================================
 * Desktop - Background and Taskbar
 * =========================================================================== */
#include "desktop.h"
#include "framebuffer.h"
#include "timer.h"
#include "rtc.h"
#include "string.h"
#include "fs.h"
#include "memory.h"

static color_t desktop_bg = COLOR_DESKTOP_BG;

/* Wallpaper state — stored as palette indices (1 byte/pixel) for compact size */
static uint8_t *wallpaper;
static int wp_w;
static int wp_h;
static bool wp_active;  /* BSS — defaults to false */

/* Paint palette (matches apps/paint.c) for decoding .pic files */
#define PIC_PAL_COUNT 16
static const color_t pic_palette[PIC_PAL_COUNT] = {
    COLOR_RGB(0, 0, 0),        COLOR_RGB(255, 255, 255),
    COLOR_RGB(180, 0, 0),      COLOR_RGB(255, 60, 60),
    COLOR_RGB(220, 120, 0),    COLOR_RGB(255, 200, 0),
    COLOR_RGB(0, 160, 0),      COLOR_RGB(0, 220, 0),
    COLOR_RGB(0, 130, 180),    COLOR_RGB(0, 180, 255),
    COLOR_RGB(0, 0, 180),      COLOR_RGB(80, 80, 255),
    COLOR_RGB(130, 0, 180),    COLOR_RGB(200, 80, 255),
    COLOR_RGB(128, 128, 128),  COLOR_RGB(200, 200, 200),
};

/* Debug output via QEMU port 0xE9 */
static void dbg_putc(char c) {
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)c), "Nd"((uint16_t)0xE9));
}
static void dbg_print(const char *s) {
    while (*s) dbg_putc(*s++);
}

void desktop_init(void) {
    wallpaper = (uint8_t *)0;
    wp_w = 0;
    wp_h = 0;
    wp_active = false;
}

void desktop_set_bg(color_t c) {
    desktop_bg = c;
}

color_t desktop_get_bg(void) {
    return desktop_bg;
}

void desktop_set_wallpaper(const char *filename) {
    dbg_print("WP: set_wallpaper called: ");
    dbg_print(filename);
    dbg_putc('\n');

    int idx = fs_find(filename);
    if (idx < 0) { dbg_print("WP: fs_find failed\n"); return; }

    struct fs_node *f = fs_get_node(idx);
    if (!f || f->type != FS_FILE || f->size < 7) { dbg_print("WP: bad file\n"); return; }
    if (!f->data) { dbg_print("WP: no data\n"); return; }

    const char *d = f->data;
    if (d[0] != 'P' || d[1] != 'I' || d[2] != 'C') { dbg_print("WP: not PIC\n"); return; }

    int w = (uint8_t)d[3] | ((uint8_t)d[4] << 8);
    int h = (uint8_t)d[5] | ((uint8_t)d[6] << 8);
    if (w <= 0 || h <= 0 || w > fb_get_width() || h > fb_get_height()) { dbg_print("WP: bad dims\n"); return; }

    /* Allocate as palette indices — only 1 byte per pixel */
    uint32_t alloc_size = (uint32_t)w * (uint32_t)h;
    uint8_t *buf = (uint8_t *)kmalloc(alloc_size);
    if (!buf) { dbg_print("WP: kmalloc failed\n"); return; }

    /* Fill with white (palette index 1) */
    mem_set(buf, 1, alloc_size);

    /* RLE decode to palette indices */
    int pos = 7;
    int size = (int)f->size;
    for (int y = 0; y < h; y++) {
        int x = 0;
        while (x < w && pos + 1 < size) {
            int run = (uint8_t)d[pos++];
            uint8_t val = (uint8_t)d[pos++];
            if (val >= PIC_PAL_COUNT) val = 0;
            for (int i = 0; i < run && x < w; i++)
                buf[y * w + x++] = val;
        }
    }

    /* Free old wallpaper if any */
    if (wp_active && wallpaper) kfree(wallpaper);

    wallpaper = buf;
    wp_w = w;
    wp_h = h;
    wp_active = true;
    dbg_print("WP: set OK\n");
}

void desktop_clear_wallpaper(void) {
    if (wp_active && wallpaper) {
        kfree(wallpaper);
        wallpaper = (uint8_t *)0;
        wp_w = 0;
        wp_h = 0;
        wp_active = false;
    }
}

bool desktop_has_wallpaper(void) {
    return wp_active;
}

void desktop_draw_background(void) {
    fb_clear(desktop_bg);
    if (wp_active && wallpaper) {
        /* Center the wallpaper on screen */
        int scr_w = fb_get_width();
        int scr_h = fb_get_height() - 28; /* above taskbar */
        int dx = (scr_w - wp_w) / 2;
        int dy = (scr_h - wp_h) / 2;
        if (dx < 0) dx = 0;
        if (dy < 0) dy = 0;

        /* Convert palette indices to color_t row-by-row into backbuffer */
        color_t *bb = fb_get_backbuf();
        for (int y = 0; y < wp_h && (dy + y) < scr_h; y++) {
            int row = dy + y;
            for (int x = 0; x < wp_w && (dx + x) < scr_w; x++) {
                uint8_t idx = wallpaper[y * wp_w + x];
                if (idx < PIC_PAL_COUNT)
                    bb[row * scr_w + (dx + x)] = pic_palette[idx];
            }
        }
    }
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

    /* Clock in right corner - real time from CMOS RTC */
    char time_str[12];
    rtc_format_time(time_str);
    int tw = str_len(time_str) * 8;
    fb_draw_string(w - tw - 8, h - 23, time_str, COLOR_WHITE, COLOR_TASKBAR);

    /* Date left of clock */
    char date_str[11];
    rtc_format_date(date_str);
    fb_draw_string(w - tw - 8 - 88, h - 23, date_str, COLOR_LIGHT_GREY, COLOR_TASKBAR);
}
