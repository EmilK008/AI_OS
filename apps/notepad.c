/* ===========================================================================
 * Notepad - Windowed text editor app
 * =========================================================================== */
#include "notepad.h"
#include "window.h"
#include "framebuffer.h"
#include "font8x16.h"
#include "fs.h"
#include "string.h"
#include "keyboard.h"
#include "timer.h"

#define NP_W        400
#define NP_H        300
#define TOOLBAR_H   24
#define STATUS_H    20
#define TEXT_X      4
#define TEXT_Y      (TOOLBAR_H + 2)
#define TEXT_W      (NP_W - 8)
#define TEXT_H      (NP_H - TOOLBAR_H - STATUS_H - 4)
#define MAX_COLS    (TEXT_W / 8)
#define MAX_ROWS    (TEXT_H / 16)
#define BUF_SIZE    4000

/* Button defs */
#define NP_BTN_COUNT 3
#define NP_BTN_W     48
#define NP_BTN_H     18
#define NP_BTN_GAP   4

/* Modes */
#define MODE_EDIT    0
#define MODE_OPEN    1  /* filename input for open */
#define MODE_SAVE    2  /* filename input for save */

/* Colors */
#define C_BG        COLOR_RGB(30, 30, 38)
#define C_TOOLBAR   COLOR_RGB(50, 50, 62)
#define C_TEXT_BG   COLOR_RGB(20, 20, 28)
#define C_TEXT_FG   COLOR_WHITE
#define C_CURSOR    COLOR_RGB(100, 200, 255)
#define C_STATUS_BG COLOR_RGB(40, 40, 52)
#define C_STATUS_FG COLOR_RGB(180, 180, 180)
#define C_BTN       COLOR_RGB(60, 60, 78)
#define C_BTN_TEXT  COLOR_WHITE
#define C_LINE_NUM  COLOR_RGB(80, 80, 100)

static int win_id = -1;
static int mode = MODE_EDIT;

/* Text buffer */
static char text_buf[BUF_SIZE + 1];
static int  text_len = 0;
static int  cursor_pos = 0;
static int  scroll_line = 0;

/* Filename */
static char filename[32];
static int  filename_len = 0;
static bool file_dirty = false;

/* Input buffer for open/save dialogs */
static char input_buf[32];
static int  input_len = 0;

static const char *btn_labels[NP_BTN_COUNT] = { "New", "Open", "Save" };

static void np_rect(color_t *buf, int cw, int ch, int x, int y, int w, int h, color_t c) {
    for (int py = y; py < y + h && py < ch; py++) {
        if (py < 0) continue;
        for (int px = x; px < x + w && px < cw; px++) {
            if (px >= 0) buf[py * cw + px] = c;
        }
    }
}

static void np_text(color_t *buf, int cw, int ch, int px, int py,
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

static void np_char_transparent(color_t *buf, int cw, int ch, int px, int py,
                                char c, color_t fg) {
    const uint8_t *glyph = font8x16[(uint8_t)c];
    for (int gy = 0; gy < 16; gy++) {
        if (py + gy < 0 || py + gy >= ch) continue;
        uint8_t bits = glyph[gy];
        for (int gx = 0; gx < 8; gx++) {
            int dx = px + gx, dy = py + gy;
            if (dx >= 0 && dx < cw && (bits & (0x80 >> gx)))
                buf[dy * cw + dx] = fg;
        }
    }
}

/* Get line/col from cursor position */
static void cursor_to_linecol(int pos, int *line, int *col) {
    int l = 0, c = 0;
    for (int i = 0; i < pos && i < text_len; i++) {
        if (text_buf[i] == '\n') { l++; c = 0; }
        else { c++; if (c >= MAX_COLS) { c = 0; l++; } }
    }
    *line = l;
    *col = c;
}

/* Get buffer position from line/col */
static int linecol_to_pos(int target_line, int target_col) {
    int l = 0, c = 0;
    for (int i = 0; i < text_len; i++) {
        if (l == target_line && c == target_col) return i;
        if (text_buf[i] == '\n') {
            if (l == target_line) return i; /* col beyond line end */
            l++; c = 0;
        } else {
            c++;
            if (c >= MAX_COLS) { c = 0; l++; }
        }
    }
    if (l == target_line) return text_len;
    return text_len;
}

static void np_new_file(void) {
    text_len = 0;
    text_buf[0] = '\0';
    cursor_pos = 0;
    scroll_line = 0;
    filename[0] = '\0';
    filename_len = 0;
    file_dirty = false;
}

static void np_open_file(const char *name) {
    int idx = fs_find(name);
    if (idx < 0) return;
    struct fs_node *f = fs_get_node(idx);
    if (!f || f->type != FS_FILE) return;

    int len = (int)f->size;
    if (len > BUF_SIZE) len = BUF_SIZE;
    mem_copy(text_buf, f->data, (uint32_t)len);
    text_len = len;
    text_buf[text_len] = '\0';
    cursor_pos = 0;
    scroll_line = 0;
    file_dirty = false;

    /* Copy filename */
    int i = 0;
    while (name[i] && i < 31) { filename[i] = name[i]; i++; }
    filename[i] = '\0';
    filename_len = i;
}

static void np_save_file(const char *name) {
    int idx = fs_find(name);
    if (idx < 0) {
        idx = fs_create(name, FS_FILE);
        if (idx < 0) return;
    }
    fs_write_file(idx, text_buf, (uint32_t)text_len);
    file_dirty = false;

    /* Update filename */
    int i = 0;
    while (name[i] && i < 31) { filename[i] = name[i]; i++; }
    filename[i] = '\0';
    filename_len = i;
}

static void insert_char(char c) {
    if (text_len >= BUF_SIZE) return;
    /* Shift right */
    for (int i = text_len; i > cursor_pos; i--)
        text_buf[i] = text_buf[i - 1];
    text_buf[cursor_pos] = c;
    text_len++;
    text_buf[text_len] = '\0';
    cursor_pos++;
    file_dirty = true;
}

static void delete_char_back(void) {
    if (cursor_pos <= 0) return;
    cursor_pos--;
    for (int i = cursor_pos; i < text_len - 1; i++)
        text_buf[i] = text_buf[i + 1];
    text_len--;
    text_buf[text_len] = '\0';
    file_dirty = true;
}

static void ensure_cursor_visible(void) {
    int line, col;
    (void)col;
    cursor_to_linecol(cursor_pos, &line, &col);
    if (line < scroll_line) scroll_line = line;
    if (line >= scroll_line + MAX_ROWS) scroll_line = line - MAX_ROWS + 1;
}

static void np_on_event(struct window *win, struct gui_event *evt) {
    (void)win;

    if (evt->type == EVT_KEY_PRESS) {
        uint8_t k = evt->key;

        /* Dialog modes */
        if (mode == MODE_OPEN || mode == MODE_SAVE) {
            if (k == 0x1B) { mode = MODE_EDIT; return; }
            if (k == '\n') {
                if (input_len > 0) {
                    input_buf[input_len] = '\0';
                    if (mode == MODE_OPEN) np_open_file(input_buf);
                    else np_save_file(input_buf);
                }
                mode = MODE_EDIT;
                return;
            }
            if (k == '\b') { if (input_len > 0) input_len--; return; }
            if (input_len < 30 && k >= 0x20 && k < 0x7F) {
                input_buf[input_len++] = (char)k;
            }
            return;
        }

        /* Normal edit mode */
        if (k == '\b') { delete_char_back(); ensure_cursor_visible(); return; }
        if (k == '\n') { insert_char('\n'); ensure_cursor_visible(); return; }

        if (k == KEY_LEFT) {
            if (cursor_pos > 0) cursor_pos--;
            ensure_cursor_visible();
            return;
        }
        if (k == KEY_RIGHT) {
            if (cursor_pos < text_len) cursor_pos++;
            ensure_cursor_visible();
            return;
        }
        if (k == KEY_UP) {
            int line, col;
            cursor_to_linecol(cursor_pos, &line, &col);
            if (line > 0) cursor_pos = linecol_to_pos(line - 1, col);
            ensure_cursor_visible();
            return;
        }
        if (k == KEY_DOWN) {
            int line, col;
            cursor_to_linecol(cursor_pos, &line, &col);
            cursor_pos = linecol_to_pos(line + 1, col);
            if (cursor_pos > text_len) cursor_pos = text_len;
            ensure_cursor_visible();
            return;
        }
        if (k == KEY_HOME) {
            /* Go to start of current line */
            while (cursor_pos > 0 && text_buf[cursor_pos - 1] != '\n')
                cursor_pos--;
            ensure_cursor_visible();
            return;
        }
        if (k == KEY_END) {
            /* Go to end of current line */
            while (cursor_pos < text_len && text_buf[cursor_pos] != '\n')
                cursor_pos++;
            ensure_cursor_visible();
            return;
        }

        /* Printable characters */
        if (k >= 0x20 && k < 0x80) {
            insert_char((char)k);
            ensure_cursor_visible();
        }
        return;
    }

    if (evt->type == EVT_MOUSE_DOWN) {
        int mx = evt->mouse_x - (win->x + BORDER_WIDTH);
        int my = evt->mouse_y - (win->y + TITLEBAR_HEIGHT);

        /* Toolbar buttons */
        if (my >= 3 && my < 3 + NP_BTN_H) {
            for (int i = 0; i < NP_BTN_COUNT; i++) {
                int bx = 4 + i * (NP_BTN_W + NP_BTN_GAP);
                if (mx >= bx && mx < bx + NP_BTN_W) {
                    if (i == 0) { /* New */
                        np_new_file();
                    } else if (i == 1) { /* Open */
                        input_len = 0;
                        mode = MODE_OPEN;
                    } else if (i == 2) { /* Save */
                        if (filename_len > 0) {
                            np_save_file(filename);
                        } else {
                            input_len = 0;
                            mode = MODE_SAVE;
                        }
                    }
                    return;
                }
            }
        }

        /* Text area click - position cursor */
        if (mx >= TEXT_X && mx < TEXT_X + TEXT_W &&
            my >= TEXT_Y && my < TEXT_Y + TEXT_H) {
            int click_col = (mx - TEXT_X) / 8;
            int click_row = (my - TEXT_Y) / 16 + scroll_line;
            int pos = linecol_to_pos(click_row, click_col);
            if (pos <= text_len) cursor_pos = pos;
        }
    }
}

void notepad_create(void) {
    if (win_id >= 0) {
        struct window *w = wm_get_window(win_id);
        if (w && w->alive) { wm_focus_window(win_id); return; }
    }
    win_id = wm_create_window("Notepad", 100, 60, NP_W, NP_H,
                               np_on_event, NULL);
    np_new_file();
}

bool notepad_is_alive(void) {
    if (win_id < 0) return false;
    struct window *w = wm_get_window(win_id);
    return (w && w->alive);
}

void notepad_render(void) {
    if (win_id < 0) return;
    struct window *win = wm_get_window(win_id);
    if (!win || !win->alive || !win->content) { win_id = -1; return; }

    int cw = win->content_w;
    int ch = win->content_h;
    color_t *buf = win->content;

    /* Background */
    for (int i = 0; i < cw * ch; i++) buf[i] = C_BG;

    /* Toolbar */
    np_rect(buf, cw, ch, 0, 0, cw, TOOLBAR_H, C_TOOLBAR);
    for (int i = 0; i < NP_BTN_COUNT; i++) {
        int bx = 4 + i * (NP_BTN_W + NP_BTN_GAP);
        np_rect(buf, cw, ch, bx, 3, NP_BTN_W, NP_BTN_H, C_BTN);
        int llen = str_len(btn_labels[i]);
        int lx = bx + (NP_BTN_W - llen * 8) / 2;
        np_text(buf, cw, ch, lx, 4, btn_labels[i], C_BTN_TEXT, C_BTN);
    }

    /* Filename in toolbar */
    if (filename_len > 0) {
        int fx = 4 + NP_BTN_COUNT * (NP_BTN_W + NP_BTN_GAP) + 8;
        np_text(buf, cw, ch, fx, 4, filename, COLOR_YELLOW, C_TOOLBAR);
        if (file_dirty) {
            np_text(buf, cw, ch, fx + filename_len * 8, 4, "*", COLOR_RED, C_TOOLBAR);
        }
    }

    /* Text area background */
    np_rect(buf, cw, ch, 0, TEXT_Y, NP_W, TEXT_H, C_TEXT_BG);

    /* Render text */
    int line = 0, col = 0;
    int cur_line = -1, cur_col = -1;
    cursor_to_linecol(cursor_pos, &cur_line, &cur_col);

    for (int i = 0; i < text_len; i++) {
        /* Draw character if visible */
        if (line >= scroll_line && line < scroll_line + MAX_ROWS) {
            int screen_row = line - scroll_line;
            int px = TEXT_X + col * 8;
            int py = TEXT_Y + screen_row * 16;

            if (text_buf[i] != '\n') {
                np_char_transparent(buf, cw, ch, px, py, text_buf[i], C_TEXT_FG);
            }
        }

        if (text_buf[i] == '\n') { line++; col = 0; }
        else { col++; if (col >= MAX_COLS) { col = 0; line++; } }
    }

    /* Blinking cursor */
    if (mode == MODE_EDIT && (timer_get_ticks() / 40) & 1) {
        if (cur_line >= scroll_line && cur_line < scroll_line + MAX_ROWS) {
            int screen_row = cur_line - scroll_line;
            int cx = TEXT_X + cur_col * 8;
            int cy = TEXT_Y + screen_row * 16;
            np_rect(buf, cw, ch, cx, cy, 2, 16, C_CURSOR);
        }
    }

    /* Status bar */
    np_rect(buf, cw, ch, 0, NP_H - STATUS_H, NP_W, STATUS_H, C_STATUS_BG);

    if (mode == MODE_OPEN || mode == MODE_SAVE) {
        const char *prompt = (mode == MODE_OPEN) ? "Open: " : "Save as: ";
        np_text(buf, cw, ch, 4, NP_H - STATUS_H + 2, prompt, COLOR_YELLOW, C_STATUS_BG);
        input_buf[input_len] = '\0';
        int px = 4 + str_len(prompt) * 8;
        np_text(buf, cw, ch, px, NP_H - STATUS_H + 2, input_buf, COLOR_WHITE, C_STATUS_BG);
        /* Input cursor */
        if ((timer_get_ticks() / 40) & 1)
            np_rect(buf, cw, ch, px + input_len * 8, NP_H - STATUS_H + 2, 8, 16, C_CURSOR);
    } else {
        /* Line:Col indicator */
        char status[48];
        char num1[12], num2[12], num3[12];

        /* Build "Ln X Col Y | Z chars" manually */
        int si = 0;
        status[si++] = 'L'; status[si++] = 'n'; status[si++] = ' ';

        /* Line number */
        int ln = cur_line + 1;
        if (ln == 0) { num1[0] = '0'; num1[1] = '\0'; }
        else {
            char tmp[12]; int ti = 0;
            int v = ln;
            while (v > 0) { tmp[ti++] = '0' + (v % 10); v /= 10; }
            int ni = 0;
            while (--ti >= 0) num1[ni++] = tmp[ti];
            num1[ni] = '\0';
        }
        for (int i = 0; num1[i]; i++) status[si++] = num1[i];

        status[si++] = ' '; status[si++] = 'C'; status[si++] = 'o';
        status[si++] = 'l'; status[si++] = ' ';

        /* Col number */
        int cn = cur_col + 1;
        if (cn == 0) { num2[0] = '0'; num2[1] = '\0'; }
        else {
            char tmp[12]; int ti = 0;
            int v = cn;
            while (v > 0) { tmp[ti++] = '0' + (v % 10); v /= 10; }
            int ni = 0;
            while (--ti >= 0) num2[ni++] = tmp[ti];
            num2[ni] = '\0';
        }
        for (int i = 0; num2[i]; i++) status[si++] = num2[i];

        status[si++] = ' '; status[si++] = '|'; status[si++] = ' ';

        /* Char count */
        if (text_len == 0) { num3[0] = '0'; num3[1] = '\0'; }
        else {
            char tmp[12]; int ti = 0;
            int v = text_len;
            while (v > 0) { tmp[ti++] = '0' + (v % 10); v /= 10; }
            int ni = 0;
            while (--ti >= 0) num3[ni++] = tmp[ti];
            num3[ni] = '\0';
        }
        for (int i = 0; num3[i]; i++) status[si++] = num3[i];

        status[si++] = ' '; status[si++] = 'c'; status[si++] = 'h';
        status[si++] = 'a'; status[si++] = 'r'; status[si++] = 's';
        status[si] = '\0';

        np_text(buf, cw, ch, 4, NP_H - STATUS_H + 2, status, C_STATUS_FG, C_STATUS_BG);
    }
}
