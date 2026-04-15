/* ===========================================================================
 * File Manager - GUI file browser with editor
 * =========================================================================== */
#include "filemgr.h"
#include "window.h"
#include "framebuffer.h"
#include "font8x16.h"
#include "fs.h"
#include "string.h"
#include "keyboard.h"
#include "timer.h"

#define FM_W        500
#define FM_H        360
#define PATH_H      20
#define TOOLBAR_H   22
#define STATUS_H    20
#define LIST_W      180
#define EDIT_X      (LIST_W + 2)
#define EDIT_W      (FM_W - LIST_W - 2)
#define LIST_Y      (PATH_H + TOOLBAR_H)
#define LIST_H      (FM_H - PATH_H - TOOLBAR_H - STATUS_H)
#define ITEM_H      16
#define MAX_VISIBLE (LIST_H / ITEM_H)
#define EDIT_MAXLEN 4000

/* Colors */
#define C_BG        COLOR_RGB(30, 30, 40)
#define C_PATH_BG   COLOR_RGB(40, 40, 55)
#define C_TOOLBAR   COLOR_RGB(50, 50, 65)
#define C_LIST_BG   COLOR_RGB(25, 25, 35)
#define C_EDIT_BG   COLOR_RGB(20, 20, 28)
#define C_SEL       COLOR_RGB(0, 70, 130)
#define C_DIR       COLOR_CYAN
#define C_FILE      COLOR_WHITE
#define C_STATUS    COLOR_RGB(40, 40, 55)
#define C_BTN       COLOR_RGB(60, 60, 80)
#define C_BTN_TEXT  COLOR_WHITE
#define C_SEPARATOR COLOR_RGB(60, 60, 80)

static void num_to_str_fm(int n, char *buf) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[12]; int i = 0;
    while (n > 0) { tmp[i++] = '0' + (n % 10); n /= 10; }
    int j = 0;
    while (--i >= 0) buf[j++] = tmp[i];
    buf[j] = '\0';
}

/* Modes */
#define MODE_BROWSE  0
#define MODE_EDIT    1
#define MODE_INPUT   2   /* text input for new file/folder name */

static int win_id = -1;
static int selected = -1;
static int scroll_off = 0;
static int mode = MODE_BROWSE;

/* Cached directory listing */
static int dir_entries[MAX_CHILDREN + 1]; /* indices into fs */
static int dir_count = 0;

/* Editor state */
static char edit_buf[EDIT_MAXLEN + 1];
static int  edit_len = 0;
static int  edit_file_idx = -1;

/* Input state (for new file/folder name) */
static char  input_buf[MAX_FILENAME];
static int   input_len = 0;
static int   input_type = 0; /* FS_FILE or FS_DIR */

/* Toolbar buttons */
#define BTN_COUNT 4
static const char *btn_labels[BTN_COUNT] = { "New File", "New Dir", "Delete", "Back" };
#define BTN_W  56
#define BTN_H  18
#define BTN_GAP 4

/* Helper: draw text into content buffer */
static void fm_text(color_t *buf, int cw, int ch, int px, int py,
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

/* Helper: fill rectangle in content buffer */
static void fm_rect(color_t *buf, int cw, int ch, int x, int y, int w, int h, color_t c) {
    for (int py = y; py < y + h && py < ch; py++) {
        if (py < 0) continue;
        for (int px = x; px < x + w && px < cw; px++) {
            if (px >= 0) buf[py * cw + px] = c;
        }
    }
}

static void refresh_dir(void) {
    dir_count = 0;
    int cwd = fs_get_cwd();
    struct fs_node *node = fs_get_node(cwd);
    if (!node) return;
    for (int i = 0; i < node->child_count && dir_count < MAX_CHILDREN; i++) {
        int ci = node->children[i];
        struct fs_node *child = fs_get_node(ci);
        if (child && child->used)
            dir_entries[dir_count++] = ci;
    }
}

static void start_edit(int idx) {
    edit_file_idx = idx;
    struct fs_node *f = fs_get_node(idx);
    if (!f || f->type != FS_FILE || !f->data) { mode = MODE_BROWSE; return; }
    edit_len = (int)f->size;
    if (edit_len > EDIT_MAXLEN) edit_len = EDIT_MAXLEN;
    mem_copy(edit_buf, f->data, (uint32_t)edit_len);
    edit_buf[edit_len] = '\0';
    mode = MODE_EDIT;
}

static void save_edit(void) {
    if (edit_file_idx >= 0) {
        fs_write_file(edit_file_idx, edit_buf, (uint32_t)edit_len);
    }
}

static void fm_on_event(struct window *win, struct gui_event *evt) {
    (void)win;
    int cw = win->content_w;
    (void)cw;

    if (evt->type == EVT_KEY_PRESS) {
        uint8_t k = evt->key;

        if (mode == MODE_INPUT) {
            if (k == '\n') {
                if (input_len > 0) {
                    input_buf[input_len] = '\0';
                    int idx = fs_create(input_buf, (uint8_t)input_type);
                    (void)idx;
                    refresh_dir();
                }
                mode = MODE_BROWSE;
                return;
            }
            if (k == 0x1B) { mode = MODE_BROWSE; return; } /* Escape */
            if (k == '\b') {
                if (input_len > 0) input_len--;
                return;
            }
            if (input_len < MAX_FILENAME - 1 && k >= 0x20 && k < 0x7F) {
                input_buf[input_len++] = (char)k;
            }
            return;
        }

        if (mode == MODE_EDIT) {
            if (k == 0x1B) { /* Escape → save and go back */
                save_edit();
                mode = MODE_BROWSE;
                refresh_dir();
                return;
            }
            if (k == '\b') {
                if (edit_len > 0) edit_len--;
                edit_buf[edit_len] = '\0';
                return;
            }
            if (k == '\n') {
                if (edit_len < EDIT_MAXLEN) {
                    edit_buf[edit_len++] = '\n';
                    edit_buf[edit_len] = '\0';
                }
                return;
            }
            if (edit_len < EDIT_MAXLEN && k >= 0x20 && k < 0x80) {
                edit_buf[edit_len++] = (char)k;
                edit_buf[edit_len] = '\0';
            }
            return;
        }

        /* MODE_BROWSE */
        if (k == KEY_UP && selected > 0) { selected--; }
        if (k == KEY_DOWN && selected < dir_count - 1) { selected++; }
        if (k == '\n' && selected >= 0 && selected < dir_count) {
            struct fs_node *f = fs_get_node(dir_entries[selected]);
            if (f) {
                if (f->type == FS_DIR) {
                    fs_change_dir(f->name);
                    refresh_dir();
                    selected = 0;
                    scroll_off = 0;
                } else {
                    start_edit(dir_entries[selected]);
                }
            }
        }
        /* Ensure selected is visible */
        if (selected < scroll_off) scroll_off = selected;
        if (selected >= scroll_off + MAX_VISIBLE) scroll_off = selected - MAX_VISIBLE + 1;
        return;
    }

    if (evt->type == EVT_MOUSE_DOWN) {
        /* Convert screen coords to content-relative */
        int mx = evt->mouse_x - (win->x + BORDER_WIDTH);
        int my = evt->mouse_y - (win->y + TITLEBAR_HEIGHT);

        /* Toolbar buttons */
        if (my >= PATH_H && my < PATH_H + TOOLBAR_H) {
            for (int i = 0; i < BTN_COUNT; i++) {
                int bx = 4 + i * (BTN_W + BTN_GAP);
                if (mx >= bx && mx < bx + BTN_W) {
                    if (i == 0) { /* New File */
                        input_len = 0;
                        input_type = FS_FILE;
                        mode = MODE_INPUT;
                    } else if (i == 1) { /* New Dir */
                        input_len = 0;
                        input_type = FS_DIR;
                        mode = MODE_INPUT;
                    } else if (i == 2) { /* Delete */
                        if (selected >= 0 && selected < dir_count) {
                            struct fs_node *f = fs_get_node(dir_entries[selected]);
                            if (f) {
                                fs_delete(f->name);
                                refresh_dir();
                                if (selected >= dir_count) selected = dir_count - 1;
                                if (mode == MODE_EDIT) mode = MODE_BROWSE;
                            }
                        }
                    } else if (i == 3) { /* Back */
                        if (mode == MODE_EDIT) {
                            save_edit();
                            mode = MODE_BROWSE;
                        }
                        fs_change_dir("..");
                        refresh_dir();
                        selected = 0;
                        scroll_off = 0;
                    }
                    return;
                }
            }
        }

        /* File list click */
        if (mx >= 0 && mx < LIST_W && my >= LIST_Y && my < LIST_Y + LIST_H) {
            int idx = (my - LIST_Y) / ITEM_H + scroll_off;
            if (idx >= 0 && idx < dir_count) {
                selected = idx;
                struct fs_node *f = fs_get_node(dir_entries[selected]);
                if (f && f->type == FS_FILE) {
                    start_edit(dir_entries[selected]);
                } else if (f && f->type == FS_DIR) {
                    fs_change_dir(f->name);
                    refresh_dir();
                    selected = 0;
                    scroll_off = 0;
                    mode = MODE_BROWSE;
                }
            }
        }
    }
}

void filemgr_create(void) {
    if (win_id >= 0) {
        struct window *w = wm_get_window(win_id);
        if (w && w->alive) { wm_focus_window(win_id); return; }
    }
    win_id = wm_create_window("Files", 80, 40, FM_W, FM_H, fm_on_event, NULL);
    selected = 0;
    scroll_off = 0;
    mode = MODE_BROWSE;
    edit_len = 0;
    edit_file_idx = -1;
    input_len = 0;
    refresh_dir();
}

bool filemgr_is_alive(void) {
    if (win_id < 0) return false;
    struct window *w = wm_get_window(win_id);
    return (w && w->alive);
}

void filemgr_render(void) {
    if (win_id < 0) return;
    struct window *win = wm_get_window(win_id);
    if (!win || !win->alive || !win->content) { win_id = -1; return; }

    int cw = win->content_w;
    int ch = win->content_h;
    color_t *buf = win->content;

    /* Background */
    for (int i = 0; i < cw * ch; i++) buf[i] = C_BG;

    /* Path bar */
    fm_rect(buf, cw, ch, 0, 0, cw, PATH_H, C_PATH_BG);
    char path[128];
    fs_get_path(fs_get_cwd(), path, 128);
    fm_text(buf, cw, ch, 4, 2, path, COLOR_CYAN, C_PATH_BG);

    /* Toolbar */
    fm_rect(buf, cw, ch, 0, PATH_H, cw, TOOLBAR_H, C_TOOLBAR);
    for (int i = 0; i < BTN_COUNT; i++) {
        int bx = 4 + i * (BTN_W + BTN_GAP);
        fm_rect(buf, cw, ch, bx, PATH_H + 2, BTN_W, BTN_H, C_BTN);
        fm_text(buf, cw, ch, bx + 4, PATH_H + 3, btn_labels[i], C_BTN_TEXT, C_BTN);
    }

    /* Separator */
    fm_rect(buf, cw, ch, LIST_W, LIST_Y, 2, LIST_H, C_SEPARATOR);

    /* File list */
    fm_rect(buf, cw, ch, 0, LIST_Y, LIST_W, LIST_H, C_LIST_BG);
    for (int i = 0; i < MAX_VISIBLE && (i + scroll_off) < dir_count; i++) {
        int idx = i + scroll_off;
        int iy = LIST_Y + i * ITEM_H;
        struct fs_node *f = fs_get_node(dir_entries[idx]);
        if (!f) continue;

        color_t bg = (idx == selected) ? C_SEL : C_LIST_BG;
        fm_rect(buf, cw, ch, 0, iy, LIST_W, ITEM_H, bg);

        /* Icon: folder or file glyph */
        char icon = (f->type == FS_DIR) ? '>' : ' ';
        color_t fc = (f->type == FS_DIR) ? C_DIR : C_FILE;
        char label[MAX_FILENAME + 2];
        label[0] = icon;
        label[1] = ' ';
        int j = 0;
        while (j < MAX_FILENAME - 2 && f->name[j]) { label[j + 2] = f->name[j]; j++; }
        label[j + 2] = '\0';
        fm_text(buf, cw, ch, 4, iy, label, fc, bg);
    }

    if (dir_count == 0) {
        fm_text(buf, cw, ch, 8, LIST_Y + 4, "(empty)", COLOR_RGB(120, 120, 120), C_LIST_BG);
    }

    /* Right pane: editor or info */
    fm_rect(buf, cw, ch, EDIT_X, LIST_Y, EDIT_W, LIST_H, C_EDIT_BG);

    if (mode == MODE_EDIT && edit_file_idx >= 0) {
        struct fs_node *f = fs_get_node(edit_file_idx);
        if (f) {
            fm_text(buf, cw, ch, EDIT_X + 4, LIST_Y + 2, f->name, COLOR_YELLOW, C_EDIT_BG);
            fm_rect(buf, cw, ch, EDIT_X, LIST_Y + 18, EDIT_W, 1, C_SEPARATOR);

            /* Render file content */
            int tx = EDIT_X + 4, ty = LIST_Y + 22;
            int max_cols = (EDIT_W - 8) / 8;
            int col = 0;
            for (int i = 0; i < edit_len && ty < LIST_Y + LIST_H - 16; i++) {
                char c = edit_buf[i];
                if (c == '\n') {
                    ty += 16;
                    col = 0;
                } else {
                    if (col < max_cols) {
                        const uint8_t *glyph = font8x16[(uint8_t)c];
                        for (int gy = 0; gy < 16 && ty + gy < ch; gy++) {
                            uint8_t bits = glyph[gy];
                            for (int gx = 0; gx < 8; gx++) {
                                int dx = tx + col * 8 + gx;
                                if (dx < cw && ty + gy >= 0)
                                    if (bits & (0x80 >> gx))
                                        buf[(ty + gy) * cw + dx] = COLOR_WHITE;
                            }
                        }
                    }
                    col++;
                    if (col >= max_cols) { col = 0; ty += 16; }
                }
            }
            /* Blinking cursor */
            if ((timer_get_ticks() / 40) & 1) {
                int cx = tx + col * 8;
                int cy = ty;
                if (cx < cw - 2 && cy < ch - 16)
                    fm_rect(buf, cw, ch, cx, cy, 8, 16, COLOR_RGB(100, 200, 255));
            }
        }
    } else if (mode == MODE_BROWSE && selected >= 0 && selected < dir_count) {
        struct fs_node *f = fs_get_node(dir_entries[selected]);
        if (f) {
            fm_text(buf, cw, ch, EDIT_X + 4, LIST_Y + 4, f->name, COLOR_YELLOW, C_EDIT_BG);
            char info[64];
            if (f->type == FS_DIR) {
                fm_text(buf, cw, ch, EDIT_X + 4, LIST_Y + 24, "Directory", C_DIR, C_EDIT_BG);
                char nbuf[12];
                num_to_str_fm(f->child_count, nbuf);
                fm_text(buf, cw, ch, EDIT_X + 4, LIST_Y + 44, nbuf, C_FILE, C_EDIT_BG);
                fm_text(buf, cw, ch, EDIT_X + 4 + str_len(nbuf) * 8, LIST_Y + 44,
                        " items", C_FILE, C_EDIT_BG);
            } else {
                fm_text(buf, cw, ch, EDIT_X + 4, LIST_Y + 24, "File", C_FILE, C_EDIT_BG);
                char nbuf[12];
                num_to_str_fm(f->size, nbuf);
                fm_text(buf, cw, ch, EDIT_X + 4, LIST_Y + 44, nbuf, C_FILE, C_EDIT_BG);
                fm_text(buf, cw, ch, EDIT_X + 4 + str_len(nbuf) * 8, LIST_Y + 44,
                        " bytes", C_FILE, C_EDIT_BG);
            }
            fm_text(buf, cw, ch, EDIT_X + 4, LIST_Y + 74,
                    "Enter to open", COLOR_RGB(120, 120, 120), C_EDIT_BG);
            (void)info;
        }
    } else {
        fm_text(buf, cw, ch, EDIT_X + 20, LIST_Y + 60,
                "Select a file", COLOR_RGB(100, 100, 100), C_EDIT_BG);
    }

    /* Status bar */
    fm_rect(buf, cw, ch, 0, ch - STATUS_H, cw, STATUS_H, C_STATUS);
    if (mode == MODE_INPUT) {
        const char *prompt = (input_type == FS_DIR) ? "Folder name: " : "File name: ";
        fm_text(buf, cw, ch, 4, ch - STATUS_H + 2, prompt, COLOR_YELLOW, C_STATUS);
        input_buf[input_len] = '\0';
        int px = 4 + str_len(prompt) * 8;
        fm_text(buf, cw, ch, px, ch - STATUS_H + 2, input_buf, COLOR_WHITE, C_STATUS);
        /* Cursor */
        if ((timer_get_ticks() / 40) & 1)
            fm_rect(buf, cw, ch, px + input_len * 8, ch - STATUS_H + 2, 8, 16,
                    COLOR_RGB(100, 200, 255));
    } else if (mode == MODE_EDIT) {
        fm_text(buf, cw, ch, 4, ch - STATUS_H + 2,
                "Editing (Esc=save & close)", COLOR_RGB(180, 180, 180), C_STATUS);
    } else {
        char nbuf[12];
        num_to_str_fm(dir_count, nbuf);
        fm_text(buf, cw, ch, 4, ch - STATUS_H + 2, nbuf, COLOR_RGB(180, 180, 180), C_STATUS);
        fm_text(buf, cw, ch, 4 + str_len(nbuf) * 8, ch - STATUS_H + 2,
                " items", COLOR_RGB(180, 180, 180), C_STATUS);
    }
}
