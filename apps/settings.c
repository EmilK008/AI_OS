/* ===========================================================================
 * Settings - System settings app with tabbed UI
 * =========================================================================== */
#include "settings.h"
#include "window.h"
#include "framebuffer.h"
#include "font8x16.h"
#include "desktop.h"
#include "mouse.h"
#include "rtc.h"
#include "memory.h"
#include "string.h"
#include "timer.h"
#include "fs.h"
#include "keyboard.h"

#define SET_W        340
#define SET_H        310
#define SIDEBAR_W    80
#define TAB_H        28
#define TAB_COUNT    4
#define PANEL_X      (SIDEBAR_W + 2)
#define PANEL_W      (SET_W - SIDEBAR_W - 2)

/* Modes */
#define MODE_NORMAL    0
#define MODE_PICK_WP   1

/* Color scheme */
#define C_BG         COLOR_RGB(30, 30, 38)
#define C_SIDEBAR    COLOR_RGB(38, 38, 48)
#define C_TAB        COLOR_RGB(45, 45, 58)
#define C_TAB_ACT    COLOR_RGB(55, 75, 110)
#define C_TAB_TEXT   COLOR_RGB(180, 180, 200)
#define C_TITLE      COLOR_RGB(100, 200, 255)
#define C_TEXT       COLOR_WHITE
#define C_DIM        COLOR_RGB(120, 120, 140)
#define C_BTN        COLOR_RGB(55, 55, 72)
#define C_BTN_ACT    COLOR_RGB(40, 120, 60)
#define C_BORDER     COLOR_RGB(60, 60, 80)

static const char *tab_labels[TAB_COUNT] = {
    "Display", "Mouse", "Clock", "About"
};

/* Desktop background color choices */
#define BG_COLORS 16
static const color_t bg_palette[BG_COLORS] = {
    COLOR_RGB(0, 128, 128),    /* Teal (default) */
    COLOR_RGB(0, 80, 120),     /* Dark Blue */
    COLOR_RGB(20, 60, 90),     /* Navy */
    COLOR_RGB(40, 80, 60),     /* Forest */
    COLOR_RGB(80, 40, 80),     /* Purple */
    COLOR_RGB(100, 50, 50),    /* Maroon */
    COLOR_RGB(60, 60, 60),     /* Charcoal */
    COLOR_RGB(30, 30, 30),     /* Near Black */
    COLOR_RGB(0, 100, 80),     /* Emerald */
    COLOR_RGB(70, 70, 100),    /* Slate */
    COLOR_RGB(100, 80, 40),    /* Brown */
    COLOR_RGB(80, 100, 40),    /* Olive */
    COLOR_RGB(50, 50, 80),     /* Indigo */
    COLOR_RGB(100, 60, 80),    /* Rose */
    COLOR_RGB(40, 90, 90),     /* Seafoam */
    COLOR_RGB(90, 90, 90),     /* Grey */
};

/* Resolution choices */
#define RES_COUNT 5
static const int res_widths[RES_COUNT]  = { 640, 800, 1024, 1280, 1920 };
static const int res_heights[RES_COUNT] = { 480, 600, 768, 720, 1080 };
static const char *res_labels[RES_COUNT] = { "640x480", "800x600", "1024x768", "1280x720", "1920x1080" };

static int win_id = -1;
static int current_tab = 0;
static int mode = MODE_NORMAL;

/* File picker state for wallpaper */
static int  picker_entries[MAX_CHILDREN];
static int  picker_count = 0;
static int  picker_sel = 0;
static int  picker_scroll = 0;
#define PICKER_VISIBLE 8

static void picker_refresh(void) {
    picker_count = 0;
    picker_sel = 0;
    picker_scroll = 0;
    int cwd = fs_get_cwd();
    struct fs_node *dir = fs_get_node(cwd);
    if (!dir) return;
    for (int i = 0; i < dir->child_count && picker_count < MAX_CHILDREN; i++) {
        int ci = dir->children[i];
        struct fs_node *child = fs_get_node(ci);
        if (child && child->used && child->type == FS_FILE)
            picker_entries[picker_count++] = ci;
    }
}

/* --- Drawing helpers --- */

static void set_rect(color_t *buf, int cw, int ch, int x, int y, int w, int h, color_t c) {
    for (int py = y; py < y + h && py < ch; py++) {
        if (py < 0) continue;
        for (int px = x; px < x + w && px < cw; px++)
            if (px >= 0) buf[py * cw + px] = c;
    }
}

static void set_text(color_t *buf, int cw, int ch, int px, int py,
                     const char *s, color_t fg, color_t bg) {
    while (*s) {
        const uint8_t *glyph = font8x16[(uint8_t)*s];
        for (int gy = 0; gy < 16; gy++) {
            if (py + gy < 0 || py + gy >= ch) continue;
            uint8_t bits = glyph[gy];
            for (int gx = 0; gx < 8; gx++) {
                int dx = px + gx, dy = py + gy;
                if (dx >= 0 && dx < cw)
                    buf[dy * cw + dx] = (bits & (0x80 >> gx)) ? fg : bg;
            }
        }
        px += 8;
        s++;
    }
}

static void set_text_nobg(color_t *buf, int cw, int ch, int px, int py,
                           const char *s, color_t fg) {
    while (*s) {
        const uint8_t *glyph = font8x16[(uint8_t)*s];
        for (int gy = 0; gy < 16; gy++) {
            if (py + gy < 0 || py + gy >= ch) continue;
            uint8_t bits = glyph[gy];
            for (int gx = 0; gx < 8; gx++) {
                int dx = px + gx, dy = py + gy;
                if (dx >= 0 && dx < cw && (bits & (0x80 >> gx)))
                    buf[dy * cw + dx] = fg;
            }
        }
        px += 8;
        s++;
    }
}

static void set_itoa(int val, char *buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[12];
    int i = 0;
    bool neg = false;
    if (val < 0) { neg = true; val = -val; }
    while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; }
    int j = 0;
    if (neg) buf[j++] = '-';
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

/* --- Event handler --- */

static void settings_on_event(struct window *win, struct gui_event *evt) {
    if (evt->type == EVT_KEY_PRESS) {
        uint8_t k = evt->key;

        /* File picker keyboard */
        if (mode == MODE_PICK_WP) {
            if (k == 0x1B) { mode = MODE_NORMAL; return; }
            if (k == KEY_UP) {
                if (picker_sel > 0) picker_sel--;
                if (picker_sel < picker_scroll) picker_scroll = picker_sel;
                return;
            }
            if (k == KEY_DOWN) {
                if (picker_sel < picker_count - 1) picker_sel++;
                if (picker_sel >= picker_scroll + PICKER_VISIBLE)
                    picker_scroll = picker_sel - PICKER_VISIBLE + 1;
                return;
            }
            if (k == '\n' && picker_count > 0) {
                struct fs_node *f = fs_get_node(picker_entries[picker_sel]);
                if (f) desktop_set_wallpaper(f->name);
                mode = MODE_NORMAL;
                return;
            }
            return;
        }
        return;
    }

    if (evt->type != EVT_MOUSE_DOWN) return;

    int mx = evt->mouse_x - (win->x + BORDER_WIDTH);
    int my = evt->mouse_y - (win->y + TITLEBAR_HEIGHT);

    /* File picker click */
    if (mode == MODE_PICK_WP && picker_count > 0) {
        int popup_w = 220;
        int popup_h = 20 + PICKER_VISIBLE * 18 + 4;
        int popup_x = (SET_W - popup_w) / 2;
        int popup_y = (SET_H - popup_h) / 2;
        int list_x = popup_x + 4;
        int list_y = popup_y + 20;
        if (mx >= list_x && mx < list_x + popup_w - 8 &&
            my >= list_y && my < list_y + PICKER_VISIBLE * 18) {
            int idx = (my - list_y) / 18 + picker_scroll;
            if (idx >= 0 && idx < picker_count) {
                struct fs_node *f = fs_get_node(picker_entries[idx]);
                if (f) desktop_set_wallpaper(f->name);
                mode = MODE_NORMAL;
            }
            return;
        }
        return;
    }

    /* Sidebar tab clicks */
    if (mx < SIDEBAR_W) {
        int tab = my / TAB_H;
        if (tab >= 0 && tab < TAB_COUNT)
            current_tab = tab;
        return;
    }

    /* Panel area clicks */
    int px = mx - PANEL_X;
    int py = my;

    switch (current_tab) {
    case 0: { /* Display */
        int grid_x = 12;
        int grid_y = 40;
        int swatch = 28;
        int gap = 4;
        int cols = 4;
        int col = (px - grid_x) / (swatch + gap);
        int row = (py - grid_y) / (swatch + gap);
        if (col >= 0 && col < cols && row >= 0 && row < BG_COLORS / cols) {
            int idx = row * cols + col;
            if (idx < BG_COLORS)
                desktop_set_bg(bg_palette[idx]);
        }

        /* Wallpaper buttons below color grid */
        int wp_btn_y = grid_y + (BG_COLORS / cols) * (swatch + gap) + 8;
        int btn_w = 80;
        int btn_h = 22;
        /* "Set Image" button */
        if (px >= 12 && px < 12 + btn_w && py >= wp_btn_y && py < wp_btn_y + btn_h) {
            picker_refresh();
            mode = MODE_PICK_WP;
        }
        /* "Clear" button */
        if (px >= 12 + btn_w + 8 && px < 12 + 2 * btn_w + 8 &&
            py >= wp_btn_y && py < wp_btn_y + btn_h) {
            desktop_clear_wallpaper();
        }

        /* Resolution buttons (2 rows: 3 + 2) */
        {
            int res_y = wp_btn_y + btn_h + 36;
            int rbtn_w = 76;
            int rbtn_h = 22;
            int rbtn_gap = 4;
            int row_gap = 4;
            for (int i = 0; i < RES_COUNT; i++) {
                int r = i / 3;
                int c = i % 3;
                int bx = 12 + c * (rbtn_w + rbtn_gap);
                int by = res_y + r * (rbtn_h + row_gap);
                if (px >= bx && px < bx + rbtn_w && py >= by && py < by + rbtn_h) {
                    if (fb_set_mode(res_widths[i], res_heights[i])) {
                        mouse_set_bounds(fb_get_width(), fb_get_height());
                        mouse_clamp();
                    }
                }
            }
        }
        break;
    }
    case 1: { /* Mouse — speed buttons */
        int btn_y = 50;
        int btn_w = 60;
        int btn_h = 24;
        int btn_gap = 8;
        for (int i = 0; i < 3; i++) {
            int bx = 12 + i * (btn_w + btn_gap);
            if (px >= bx && px < bx + btn_w && py >= btn_y && py < btn_y + btn_h) {
                mouse_set_speed(i + 1);
            }
        }
        break;
    }
    case 2: { /* Clock — toggle buttons */
        int btn_y = 50;
        int btn_w = 80;
        int btn_h = 24;
        int btn_gap = 12;
        if (px >= 12 && px < 12 + btn_w && py >= btn_y && py < btn_y + btn_h) {
            rtc_set_24h(true);
        }
        if (px >= 12 + btn_w + btn_gap && px < 12 + 2 * btn_w + btn_gap &&
            py >= btn_y && py < btn_y + btn_h) {
            rtc_set_24h(false);
        }
        break;
    }
    default:
        break;
    }
}

void settings_create(void) {
    if (win_id >= 0) {
        struct window *w = wm_get_window(win_id);
        if (w && w->alive) { wm_focus_window(win_id); return; }
    }
    win_id = wm_create_window("Settings", 140, 60, SET_W, SET_H,
                               settings_on_event, NULL);
    current_tab = 0;
    mode = MODE_NORMAL;
}

bool settings_is_alive(void) {
    if (win_id < 0) return false;
    struct window *w = wm_get_window(win_id);
    return (w && w->alive);
}

void settings_render(void) {
    if (win_id < 0) return;
    struct window *win = wm_get_window(win_id);
    if (!win || !win->alive || !win->content) { win_id = -1; return; }

    int cw = win->content_w;
    int ch = win->content_h;
    color_t *buf = win->content;

    /* Background */
    for (int i = 0; i < cw * ch; i++) buf[i] = C_BG;

    /* Sidebar */
    set_rect(buf, cw, ch, 0, 0, SIDEBAR_W, ch, C_SIDEBAR);

    for (int i = 0; i < TAB_COUNT; i++) {
        int ty = i * TAB_H;
        color_t bg = (i == current_tab) ? C_TAB_ACT : C_TAB;
        set_rect(buf, cw, ch, 0, ty, SIDEBAR_W, TAB_H - 1, bg);
        int tlen = str_len(tab_labels[i]);
        int tx = (SIDEBAR_W - tlen * 8) / 2;
        set_text(buf, cw, ch, tx, ty + 6, tab_labels[i], C_TAB_TEXT, bg);
    }

    /* Divider line */
    set_rect(buf, cw, ch, SIDEBAR_W, 0, 2, ch, C_BORDER);

    /* Panel content */
    int px0 = PANEL_X + 12;

    switch (current_tab) {
    case 0: { /* Display */
        set_text_nobg(buf, cw, ch, px0, 8, "Display Settings", C_TITLE);
        set_text_nobg(buf, cw, ch, px0, 28, "Wallpaper Color:", C_TEXT);

        int grid_x = PANEL_X + 12;
        int grid_y = 40;
        int swatch = 28;
        int gap = 4;
        int cols = 4;
        color_t current_bg = desktop_get_bg();

        for (int i = 0; i < BG_COLORS; i++) {
            int col = i % cols;
            int row = i / cols;
            int sx = grid_x + col * (swatch + gap);
            int sy = grid_y + row * (swatch + gap);
            set_rect(buf, cw, ch, sx, sy, swatch, swatch, bg_palette[i]);
            if (bg_palette[i] == current_bg) {
                set_rect(buf, cw, ch, sx - 1, sy - 1, swatch + 2, 1, COLOR_WHITE);
                set_rect(buf, cw, ch, sx - 1, sy + swatch, swatch + 2, 1, COLOR_WHITE);
                set_rect(buf, cw, ch, sx - 1, sy - 1, 1, swatch + 2, COLOR_WHITE);
                set_rect(buf, cw, ch, sx + swatch, sy - 1, 1, swatch + 2, COLOR_WHITE);
            }
        }

        /* Wallpaper image buttons */
        int wp_btn_y = grid_y + (BG_COLORS / cols) * (swatch + gap) + 8;
        int btn_w = 80;
        int btn_h = 22;

        set_text_nobg(buf, cw, ch, px0, wp_btn_y - 16, "Wallpaper Image:", C_TEXT);

        /* "Set Image" button */
        set_rect(buf, cw, ch, grid_x, wp_btn_y, btn_w, btn_h, C_BTN);
        set_text(buf, cw, ch, grid_x + 4, wp_btn_y + 3, "Set Image", C_TEXT, C_BTN);

        /* "Clear" button */
        int clr_x = grid_x + btn_w + 8;
        color_t clr_bg = desktop_has_wallpaper() ? C_BTN : COLOR_RGB(40, 40, 50);
        color_t clr_fg = desktop_has_wallpaper() ? C_TEXT : C_DIM;
        set_rect(buf, cw, ch, clr_x, wp_btn_y, btn_w, btn_h, clr_bg);
        set_text(buf, cw, ch, clr_x + 16, wp_btn_y + 3, "Clear", clr_fg, clr_bg);

        /* Status */
        if (desktop_has_wallpaper()) {
            set_text_nobg(buf, cw, ch, px0, wp_btn_y + btn_h + 4,
                          "Image set", COLOR_RGB(80, 220, 80));
        }

        /* Resolution buttons */
        int info_y = wp_btn_y + btn_h + 22;
        set_text_nobg(buf, cw, ch, px0, info_y, "Resolution:", C_TEXT);

        /* Resolution buttons (2 rows: 3 + 2) */
        {
            int res_y = info_y + 14;
            int rbtn_w = 76;
            int rbtn_h = 22;
            int rbtn_gap = 4;
            int row_gap = 4;
            int cur_w = fb_get_width();
            int cur_h = fb_get_height();
            for (int i = 0; i < RES_COUNT; i++) {
                int r = i / 3;
                int c = i % 3;
                int bx = grid_x + c * (rbtn_w + rbtn_gap);
                int by = res_y + r * (rbtn_h + row_gap);
                color_t bc = (cur_w == res_widths[i] && cur_h == res_heights[i])
                    ? C_BTN_ACT : C_BTN;
                set_rect(buf, cw, ch, bx, by, rbtn_w, rbtn_h, bc);
                int tlen = str_len(res_labels[i]);
                int tx = bx + (rbtn_w - tlen * 8) / 2;
                set_text(buf, cw, ch, tx, by + 3, res_labels[i], C_TEXT, bc);
            }
        }
        break;
    }
    case 1: { /* Mouse */
        set_text_nobg(buf, cw, ch, px0, 8, "Mouse Settings", C_TITLE);
        set_text_nobg(buf, cw, ch, px0, 30, "Pointer Speed:", C_TEXT);

        int btn_y = 50;
        int btn_w = 60;
        int btn_h = 24;
        int btn_gap = 8;
        int speed = mouse_get_speed();
        static const char *speed_labels[] = { "Slow", "Normal", "Fast" };

        for (int i = 0; i < 3; i++) {
            int bx = PANEL_X + 12 + i * (btn_w + btn_gap);
            color_t bc = (speed == i + 1) ? C_BTN_ACT : C_BTN;
            set_rect(buf, cw, ch, bx, btn_y, btn_w, btn_h, bc);
            int tlen = str_len(speed_labels[i]);
            int tx = bx + (btn_w - tlen * 8) / 2;
            set_text(buf, cw, ch, tx, btn_y + 4, speed_labels[i], C_TEXT, bc);
        }

        set_text_nobg(buf, cw, ch, px0, btn_y + btn_h + 16, "Slow = precise", C_DIM);
        set_text_nobg(buf, cw, ch, px0, btn_y + btn_h + 34, "Fast = less movement", C_DIM);
        break;
    }
    case 2: { /* Clock */
        set_text_nobg(buf, cw, ch, px0, 8, "Clock Settings", C_TITLE);
        set_text_nobg(buf, cw, ch, px0, 30, "Time Format:", C_TEXT);

        int btn_y = 50;
        int btn_w = 80;
        int btn_h = 24;
        int btn_gap = 12;
        bool is_24h = rtc_get_24h();

        int bx1 = PANEL_X + 12;
        color_t bc1 = is_24h ? C_BTN_ACT : C_BTN;
        set_rect(buf, cw, ch, bx1, btn_y, btn_w, btn_h, bc1);
        set_text(buf, cw, ch, bx1 + 16, btn_y + 4, "24 Hr", C_TEXT, bc1);

        int bx2 = bx1 + btn_w + btn_gap;
        color_t bc2 = !is_24h ? C_BTN_ACT : C_BTN;
        set_rect(buf, cw, ch, bx2, btn_y, btn_w, btn_h, bc2);
        set_text(buf, cw, ch, bx2 + 16, btn_y + 4, "12 Hr", C_TEXT, bc2);

        char time_str[12];
        rtc_format_time(time_str);
        set_text_nobg(buf, cw, ch, px0, btn_y + btn_h + 20, "Current:", C_DIM);
        set_text_nobg(buf, cw, ch, px0 + 80, btn_y + btn_h + 20, time_str, COLOR_YELLOW);
        break;
    }
    case 3: { /* About */
        set_text_nobg(buf, cw, ch, px0, 8, "About AI_OS", C_TITLE);

        int y = 36;
        set_text_nobg(buf, cw, ch, px0, y, "Version: 0.3", C_TEXT);
        y += 22;
        set_text_nobg(buf, cw, ch, px0, y, "Arch: x86 (i686)", C_TEXT);
        y += 22;
        set_text_nobg(buf, cw, ch, px0, y, "Mode: Protected Mode", C_TEXT);
        y += 30;

        set_text_nobg(buf, cw, ch, px0, y, "Memory:", C_TITLE);
        y += 20;

        char nbuf[16];
        uint32_t total_kb = memory_get_total() / 1024;
        uint32_t free_kb = memory_get_free() / 1024;
        uint32_t used_kb = total_kb - free_kb;

        set_text_nobg(buf, cw, ch, px0, y, "Total:", C_DIM);
        set_itoa((int)total_kb, nbuf);
        set_text_nobg(buf, cw, ch, px0 + 64, y, nbuf, C_TEXT);
        set_text_nobg(buf, cw, ch, px0 + 64 + str_len(nbuf) * 8, y, " KB", C_TEXT);
        y += 18;

        set_text_nobg(buf, cw, ch, px0, y, "Used:", C_DIM);
        set_itoa((int)used_kb, nbuf);
        set_text_nobg(buf, cw, ch, px0 + 64, y, nbuf, C_TEXT);
        set_text_nobg(buf, cw, ch, px0 + 64 + str_len(nbuf) * 8, y, " KB", C_TEXT);
        y += 18;

        set_text_nobg(buf, cw, ch, px0, y, "Free:", C_DIM);
        set_itoa((int)free_kb, nbuf);
        set_text_nobg(buf, cw, ch, px0 + 64, y, nbuf, COLOR_RGB(80, 220, 80));
        set_text_nobg(buf, cw, ch, px0 + 64 + str_len(nbuf) * 8, y, " KB", COLOR_RGB(80, 220, 80));
        y += 30;

        set_text_nobg(buf, cw, ch, px0, y, "Built by Claude", C_DIM);
        break;
    }
    }

    /* File picker popup overlay (for wallpaper) */
    if (mode == MODE_PICK_WP) {
        int popup_w = 220;
        int popup_h = 20 + PICKER_VISIBLE * 18 + 4;
        int popup_x = (SET_W - popup_w) / 2;
        int popup_y = (SET_H - popup_h) / 2;

        /* Background */
        set_rect(buf, cw, ch, popup_x, popup_y, popup_w, popup_h, COLOR_RGB(30, 30, 40));
        /* Border */
        set_rect(buf, cw, ch, popup_x, popup_y, popup_w, 1, COLOR_RGB(100, 100, 120));
        set_rect(buf, cw, ch, popup_x, popup_y + popup_h - 1, popup_w, 1, COLOR_RGB(100, 100, 120));
        set_rect(buf, cw, ch, popup_x, popup_y, 1, popup_h, COLOR_RGB(100, 100, 120));
        set_rect(buf, cw, ch, popup_x + popup_w - 1, popup_y, 1, popup_h, COLOR_RGB(100, 100, 120));
        /* Title */
        set_text(buf, cw, ch, popup_x + 8, popup_y + 2, "Set Wallpaper",
                 COLOR_YELLOW, COLOR_RGB(30, 30, 40));

        if (picker_count == 0) {
            set_text(buf, cw, ch, popup_x + 8, popup_y + 22, "(no files)",
                     COLOR_RGB(120, 120, 120), COLOR_RGB(30, 30, 40));
        } else {
            int list_y = popup_y + 20;
            int visible = picker_count - picker_scroll;
            if (visible > PICKER_VISIBLE) visible = PICKER_VISIBLE;
            for (int i = 0; i < visible; i++) {
                int fi = picker_scroll + i;
                int iy = list_y + i * 18;
                struct fs_node *f = fs_get_node(picker_entries[fi]);
                if (!f) continue;
                color_t row_bg = (fi == picker_sel)
                    ? COLOR_RGB(50, 80, 130) : COLOR_RGB(30, 30, 40);
                set_rect(buf, cw, ch, popup_x + 2, iy, popup_w - 4, 18, row_bg);
                set_text(buf, cw, ch, popup_x + 8, iy + 1, f->name, C_TEXT, row_bg);
            }
        }
        set_text(buf, cw, ch, popup_x + 8, popup_y + popup_h - 18,
                 "Enter=Set  Esc=Cancel", COLOR_RGB(100, 100, 100), COLOR_RGB(30, 30, 40));
    }
}
