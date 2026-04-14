/* ===========================================================================
 * Terminal Window - Text buffer + pixel rendering for shell in GUI
 * =========================================================================== */
#include "terminal.h"
#include "framebuffer.h"
#include "font8x16.h"
#include "window.h"
#include "keyboard.h"
#include "string.h"
#include "timer.h"

/* Standard VGA 16-color palette -> RGB */
static const color_t vga_palette[16] = {
    0x00000000, /* black */
    0x000000AA, /* blue */
    0x0000AA00, /* green */
    0x0000AAAA, /* cyan */
    0x00AA0000, /* red */
    0x00AA00AA, /* magenta */
    0x00AA5500, /* brown */
    0x00AAAAAA, /* light grey */
    0x00555555, /* dark grey */
    0x005555FF, /* light blue */
    0x0055FF55, /* light green */
    0x0055FFFF, /* light cyan */
    0x00FF5555, /* light red */
    0x00FF55FF, /* light magenta */
    0x00FFFF55, /* yellow */
    0x00FFFFFF, /* white */
};

static char    term_chars[TERM_ROWS][TERM_COLS];
static uint8_t term_colors[TERM_ROWS][TERM_COLS];
static int     term_cx = 0, term_cy = 0;
static uint8_t term_current_color;
static int     term_window_id = -1;

static void term_scroll(void) {
    if (term_cy >= TERM_ROWS) {
        for (int y = 0; y < TERM_ROWS - 1; y++) {
            mem_copy(term_chars[y], term_chars[y + 1], TERM_COLS);
            mem_copy(term_colors[y], term_colors[y + 1], TERM_COLS);
        }
        mem_set(term_chars[TERM_ROWS - 1], ' ', TERM_COLS);
        mem_set(term_colors[TERM_ROWS - 1], term_current_color, TERM_COLS);
        term_cy = TERM_ROWS - 1;
    }
}

void terminal_create(void) {
    /* Init terminal buffer */
    term_current_color = 0x07; /* light grey on black */
    mem_set(term_chars, ' ', sizeof(term_chars));
    mem_set(term_colors, term_current_color, sizeof(term_colors));
    term_cx = 0;
    term_cy = 0;

    /* Create window: 78 cols * 8px = 624, 24 rows * 16px = 384 */
    int content_w = TERM_COLS * 8;
    int content_h = TERM_ROWS * 16;
    term_window_id = wm_create_window("Terminal", 4, 4,
                                       content_w, content_h,
                                       terminal_on_event, NULL);
}

void terminal_on_event(struct window *win, struct gui_event *evt) {
    (void)win;
    if (evt->type == EVT_KEY_PRESS) {
        keyboard_inject_char(evt->key);
    }
}

void terminal_putchar_redirect(char c) {
    if (c == '\n') {
        term_cx = 0;
        term_cy++;
    } else if (c == '\r') {
        term_cx = 0;
    } else if (c == '\t') {
        term_cx = (term_cx + 8) & ~7;
        if (term_cx >= TERM_COLS) {
            term_cx = 0;
            term_cy++;
        }
    } else if (c == '\b') {
        if (term_cx > 0) {
            term_cx--;
            term_chars[term_cy][term_cx] = ' ';
            term_colors[term_cy][term_cx] = term_current_color;
        }
    } else {
        if (term_cy >= TERM_ROWS) term_scroll();
        term_chars[term_cy][term_cx] = c;
        term_colors[term_cy][term_cx] = term_current_color;
        term_cx++;
        if (term_cx >= TERM_COLS) {
            term_cx = 0;
            term_cy++;
        }
    }
    term_scroll();
}

void terminal_clear_redirect(void) {
    mem_set(term_chars, ' ', sizeof(term_chars));
    mem_set(term_colors, term_current_color, sizeof(term_colors));
    term_cx = 0;
    term_cy = 0;
}

void terminal_set_color_redirect(uint8_t color) {
    term_current_color = color;
}

void terminal_putchar_at_redirect(int x, int y, char c, uint8_t color) {
    if (x >= 0 && x < TERM_COLS && y >= 0 && y < TERM_ROWS) {
        term_chars[y][x] = c;
        term_colors[y][x] = color;
    }
}

void terminal_set_cursor_redirect(int x, int y) {
    term_cx = x;
    term_cy = y;
}

bool terminal_is_alive(void) {
    if (term_window_id < 0) return false;
    struct window *win = wm_get_window(term_window_id);
    return (win && win->alive);
}

void terminal_reopen(void) {
    terminal_create();
}

void terminal_render(void) {
    struct window *win = wm_get_window(term_window_id);
    if (!win || !win->content) return;

    int cw = win->content_w;

    for (int row = 0; row < TERM_ROWS; row++) {
        for (int col = 0; col < TERM_COLS; col++) {
            char ch = term_chars[row][col];
            uint8_t attr = term_colors[row][col];
            color_t fg = vga_palette[attr & 0x0F];
            color_t bg = vga_palette[(attr >> 4) & 0x0F];

            /* Render 8x16 glyph directly into window content buffer */
            const uint8_t *glyph = font8x16[(uint8_t)ch];
            int px = col * 8;
            int py = row * 16;

            for (int gy = 0; gy < 16; gy++) {
                uint8_t bits = glyph[gy];
                color_t *dest = win->content + (py + gy) * cw + px;
                for (int gx = 0; gx < 8; gx++) {
                    *dest++ = (bits & (0x80 >> gx)) ? fg : bg;
                }
            }
        }
    }

    /* Draw cursor (blinking block) */
    uint32_t ticks = timer_get_ticks();
    if ((ticks / 50) & 1) { /* blink at ~1Hz */
        int px = term_cx * 8;
        int py = term_cy * 16;
        if (term_cy < TERM_ROWS && term_cx < TERM_COLS) {
            for (int gy = 14; gy < 16; gy++) {
                color_t *dest = win->content + (py + gy) * cw + px;
                for (int gx = 0; gx < 8; gx++) {
                    *dest++ = COLOR_WHITE;
                }
            }
        }
    }
}
