/* ===========================================================================
 * Task Manager - Shows open windows and processes, allows closing/killing
 * =========================================================================== */
#include "taskmanager.h"
#include "window.h"
#include "framebuffer.h"
#include "font8x16.h"
#include "process.h"
#include "string.h"
#include "memory.h"

#define TM_W    300
#define TM_H    340

/* Color scheme */
#define C_BG        COLOR_RGB(25, 25, 35)
#define C_HEADER    COLOR_RGB(35, 35, 50)
#define C_ROW       COLOR_RGB(30, 30, 42)
#define C_ROW_ALT   COLOR_RGB(34, 34, 46)
#define C_ROW_SEL   COLOR_RGB(45, 65, 100)
#define C_TITLE     COLOR_RGB(100, 200, 255)
#define C_TEXT      COLOR_WHITE
#define C_DIM       COLOR_RGB(120, 120, 140)
#define C_BTN       COLOR_RGB(55, 55, 72)
#define C_BTN_END   COLOR_RGB(160, 50, 50)
#define C_BTN_KILL  COLOR_RGB(180, 60, 40)
#define C_BORDER    COLOR_RGB(60, 60, 80)
#define C_GREEN     COLOR_RGB(80, 200, 80)
#define C_YELLOW    COLOR_RGB(200, 200, 60)
#define C_RED       COLOR_RGB(200, 80, 80)

/* Selection modes */
#define SEL_NONE  0
#define SEL_APP   1
#define SEL_PROC  2

static int win_id = -1;
static int sel_mode = SEL_NONE;
static int sel_idx = -1;       /* index into the displayed list */
static int scroll_app = 0;
static int scroll_proc = 0;

/* --- Drawing helpers --- */

static void tm_rect(color_t *buf, int cw, int ch,
                    int x, int y, int w, int h, color_t c) {
    for (int py = y; py < y + h && py < ch; py++) {
        if (py < 0) continue;
        for (int px = x; px < x + w && px < cw; px++)
            if (px >= 0) buf[py * cw + px] = c;
    }
}

static void tm_text(color_t *buf, int cw, int ch, int px, int py,
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

static void tm_text_nobg(color_t *buf, int cw, int ch, int px, int py,
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

static void tm_itoa(int val, char *buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    char tmp[12];
    int i = 0;
    if (val < 0) { val = -val; }
    while (val > 0) { tmp[i++] = '0' + (val % 10); val /= 10; }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

/* Layout constants */
#define APPS_Y       24       /* Start of apps section */
#define ROW_H        18       /* Row height */
#define MAX_VISIBLE  6        /* Max visible rows per section */
#define BTN_W        76
#define BTN_H        20
#define BTN_X        (TM_W - BTN_W - 8)

/* --- Collect live windows --- */
static int app_ids[MAX_WINDOWS];
static int app_count = 0;

static void refresh_apps(void) {
    app_count = 0;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        struct window *w = wm_get_window(i);
        if (w && w->alive)
            app_ids[app_count++] = i;
    }
}

/* --- Collect active processes --- */
static int proc_pids[MAX_PROCESSES];
static int proc_count = 0;

static void refresh_procs(void) {
    proc_count = 0;
    struct process *table = process_get_table();
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (table[i].state != PROC_UNUSED && table[i].state != PROC_DEAD)
            proc_pids[proc_count++] = i;
    }
}

/* --- Event handler --- */

static void taskmanager_on_event(struct window *win, struct gui_event *evt) {
    if (evt->type == EVT_KEY_PRESS) {
        uint8_t k = evt->key;
        if (k == 0x1B) { /* Escape - deselect */
            sel_mode = SEL_NONE;
            sel_idx = -1;
        }
        return;
    }

    if (evt->type != EVT_MOUSE_DOWN) return;

    int mx = evt->mouse_x - (win->x + BORDER_WIDTH);
    int my = evt->mouse_y - (win->y + TITLEBAR_HEIGHT);

    /* Figure out section positions dynamically */
    int apps_list_y = APPS_Y;
    int apps_visible = app_count < MAX_VISIBLE ? app_count : MAX_VISIBLE;
    int apps_end_y = apps_list_y + apps_visible * ROW_H;
    int btn_area_y = apps_end_y + 2;

    int procs_header_y = btn_area_y + BTN_H + 8;
    int procs_list_y = procs_header_y + 20;
    int procs_visible = proc_count < MAX_VISIBLE ? proc_count : MAX_VISIBLE;
    int procs_end_y = procs_list_y + procs_visible * ROW_H;
    int proc_btn_y = procs_end_y + 2;

    /* Click on app row */
    if (my >= apps_list_y && my < apps_end_y) {
        int idx = (my - apps_list_y) / ROW_H + scroll_app;
        if (idx >= 0 && idx < app_count) {
            sel_mode = SEL_APP;
            sel_idx = idx;
        }
        return;
    }

    /* Click "End Task" button */
    if (my >= btn_area_y && my < btn_area_y + BTN_H &&
        mx >= BTN_X && mx < BTN_X + BTN_W) {
        if (sel_mode == SEL_APP && sel_idx >= 0 && sel_idx < app_count) {
            int wid = app_ids[sel_idx];
            /* Don't close our own window */
            if (wid != win_id) {
                wm_destroy_window(wid);
                sel_mode = SEL_NONE;
                sel_idx = -1;
            }
        }
        return;
    }

    /* Click on process row */
    if (my >= procs_list_y && my < procs_end_y) {
        int idx = (my - procs_list_y) / ROW_H + scroll_proc;
        if (idx >= 0 && idx < proc_count) {
            sel_mode = SEL_PROC;
            sel_idx = idx;
        }
        return;
    }

    /* Click "Kill Process" button */
    if (my >= proc_btn_y && my < proc_btn_y + BTN_H &&
        mx >= BTN_X && mx < BTN_X + BTN_W) {
        if (sel_mode == SEL_PROC && sel_idx >= 0 && sel_idx < proc_count) {
            struct process *table = process_get_table();
            int pi = proc_pids[sel_idx];
            /* Don't kill PID 0 */
            if (table[pi].pid > 0) {
                process_kill(table[pi].pid);
                sel_mode = SEL_NONE;
                sel_idx = -1;
            }
        }
        return;
    }

    /* Click elsewhere deselects */
    sel_mode = SEL_NONE;
    sel_idx = -1;
}

/* --- Public API --- */

void taskmanager_create(void) {
    if (win_id >= 0) {
        struct window *w = wm_get_window(win_id);
        if (w && w->alive) { wm_focus_window(win_id); return; }
    }
    win_id = wm_create_window("Task Manager", 160, 50, TM_W, TM_H,
                               taskmanager_on_event, NULL);
    sel_mode = SEL_NONE;
    sel_idx = -1;
    scroll_app = 0;
    scroll_proc = 0;
}

bool taskmanager_is_alive(void) {
    if (win_id < 0) return false;
    struct window *w = wm_get_window(win_id);
    return (w && w->alive);
}

void taskmanager_render(void) {
    if (win_id < 0) return;
    struct window *win = wm_get_window(win_id);
    if (!win || !win->alive || !win->content) { win_id = -1; return; }

    int cw = win->content_w;
    int ch = win->content_h;
    color_t *buf = win->content;

    /* Refresh data */
    refresh_apps();
    refresh_procs();

    /* Clamp selection */
    if (sel_mode == SEL_APP && sel_idx >= app_count) {
        sel_idx = app_count - 1;
        if (sel_idx < 0) sel_mode = SEL_NONE;
    }
    if (sel_mode == SEL_PROC && sel_idx >= proc_count) {
        sel_idx = proc_count - 1;
        if (sel_idx < 0) sel_mode = SEL_NONE;
    }

    /* Background */
    for (int i = 0; i < cw * ch; i++) buf[i] = C_BG;

    /* === APPS SECTION === */
    int y = 2;
    tm_text_nobg(buf, cw, ch, 8, y, "Applications", C_TITLE);
    /* Count label */
    {
        char nbuf[8];
        tm_itoa(app_count, nbuf);
        int tx = TM_W - 8 - (str_len(nbuf) + 1) * 8;
        tm_text_nobg(buf, cw, ch, tx, y, "(", C_DIM);
        tm_text_nobg(buf, cw, ch, tx + 8, y, nbuf, C_DIM);
        tm_text_nobg(buf, cw, ch, tx + 8 + str_len(nbuf) * 8, y, ")", C_DIM);
    }

    y = APPS_Y;
    int apps_visible = app_count < MAX_VISIBLE ? app_count : MAX_VISIBLE;

    if (app_count == 0) {
        tm_text_nobg(buf, cw, ch, 16, y + 2, "(none)", C_DIM);
        y += ROW_H;
    } else {
        for (int i = 0; i < apps_visible; i++) {
            int ai = i + scroll_app;
            if (ai >= app_count) break;
            struct window *aw = wm_get_window(app_ids[ai]);
            if (!aw) continue;

            color_t row_bg = (sel_mode == SEL_APP && sel_idx == ai)
                ? C_ROW_SEL : ((i & 1) ? C_ROW_ALT : C_ROW);
            tm_rect(buf, cw, ch, 4, y, cw - 8, ROW_H, row_bg);

            /* Window title */
            tm_text(buf, cw, ch, 8, y + 1, aw->title, C_TEXT, row_bg);

            /* Window ID */
            char idbuf[8];
            tm_itoa(app_ids[ai], idbuf);
            int idw = str_len(idbuf) * 8;
            tm_text(buf, cw, ch, cw - 8 - idw, y + 1, idbuf, C_DIM, row_bg);

            y += ROW_H;
        }
    }

    /* "End Task" button */
    int btn_y = y + 2;
    {
        bool can_end = (sel_mode == SEL_APP && sel_idx >= 0 && sel_idx < app_count
                        && app_ids[sel_idx] != win_id);
        color_t bc = can_end ? C_BTN_END : C_BTN;
        color_t fc = can_end ? C_TEXT : C_DIM;
        tm_rect(buf, cw, ch, BTN_X, btn_y, BTN_W, BTN_H, bc);
        tm_text(buf, cw, ch, BTN_X + 6, btn_y + 2, "End Task", fc, bc);
    }

    /* === PROCESSES SECTION === */
    y = btn_y + BTN_H + 8;
    tm_text_nobg(buf, cw, ch, 8, y, "Processes", C_TITLE);
    {
        char nbuf[8];
        tm_itoa(proc_count, nbuf);
        int tx = TM_W - 8 - (str_len(nbuf) + 1) * 8;
        tm_text_nobg(buf, cw, ch, tx, y, "(", C_DIM);
        tm_text_nobg(buf, cw, ch, tx + 8, y, nbuf, C_DIM);
        tm_text_nobg(buf, cw, ch, tx + 8 + str_len(nbuf) * 8, y, ")", C_DIM);
    }
    y += 20;

    /* Column headers */
    tm_text_nobg(buf, cw, ch, 8, y, "PID", C_DIM);
    tm_text_nobg(buf, cw, ch, 48, y, "State", C_DIM);
    tm_text_nobg(buf, cw, ch, 120, y, "Name", C_DIM);
    y += 16;
    tm_rect(buf, cw, ch, 4, y, cw - 8, 1, C_BORDER);
    y += 2;

    int procs_visible = proc_count < MAX_VISIBLE ? proc_count : MAX_VISIBLE;
    struct process *table = process_get_table();
    int cur_pid = process_get_current();

    if (proc_count == 0) {
        tm_text_nobg(buf, cw, ch, 16, y + 2, "(none)", C_DIM);
        y += ROW_H;
    } else {
        for (int i = 0; i < procs_visible; i++) {
            int pi = i + scroll_proc;
            if (pi >= proc_count) break;
            int idx = proc_pids[pi];
            struct process *p = &table[idx];

            color_t row_bg = (sel_mode == SEL_PROC && sel_idx == pi)
                ? C_ROW_SEL : ((i & 1) ? C_ROW_ALT : C_ROW);
            tm_rect(buf, cw, ch, 4, y, cw - 8, ROW_H, row_bg);

            /* PID */
            char pidbuf[8];
            tm_itoa((int)p->pid, pidbuf);
            tm_text(buf, cw, ch, 8, y + 1, pidbuf, C_TEXT, row_bg);

            /* State */
            const char *state_str;
            color_t state_clr;
            switch (p->state) {
            case PROC_RUNNING:  state_str = "RUN";   state_clr = C_GREEN;  break;
            case PROC_READY:    state_str = "READY"; state_clr = C_GREEN;  break;
            case PROC_SLEEPING: state_str = "SLEEP"; state_clr = C_YELLOW; break;
            default:            state_str = "???";   state_clr = C_DIM;    break;
            }
            tm_text(buf, cw, ch, 48, y + 1, state_str, state_clr, row_bg);

            /* Name */
            tm_text(buf, cw, ch, 120, y + 1, p->name, C_TEXT, row_bg);

            /* Current process marker */
            if ((int)p->pid == cur_pid) {
                tm_text(buf, cw, ch, cw - 20, y + 1, "*", C_YELLOW, row_bg);
            }

            y += ROW_H;
        }
    }

    /* "Kill" button */
    int proc_btn_y = y + 2;
    {
        bool can_kill = false;
        if (sel_mode == SEL_PROC && sel_idx >= 0 && sel_idx < proc_count) {
            int pi = proc_pids[sel_idx];
            can_kill = (table[pi].pid > 0);
        }
        color_t bc = can_kill ? C_BTN_KILL : C_BTN;
        color_t fc = can_kill ? C_TEXT : C_DIM;
        tm_rect(buf, cw, ch, BTN_X, proc_btn_y, BTN_W, BTN_H, bc);
        tm_text(buf, cw, ch, BTN_X + 2, proc_btn_y + 2, "Kill Proc", fc, bc);
    }

    /* Footer — memory info */
    {
        int fy = ch - 18;
        tm_rect(buf, cw, ch, 0, fy - 2, cw, 20, C_HEADER);
        uint32_t free_kb = memory_get_free() / 1024;
        uint32_t total_kb = memory_get_total() / 1024;
        char fbuf[16], tbuf[16];
        tm_itoa((int)free_kb, fbuf);
        tm_itoa((int)total_kb, tbuf);

        tm_text(buf, cw, ch, 8, fy, "Mem:", C_DIM, C_HEADER);
        int tx = 8 + 4 * 8;
        tm_text(buf, cw, ch, tx, fy, fbuf, C_GREEN, C_HEADER);
        tx += str_len(fbuf) * 8;
        tm_text(buf, cw, ch, tx, fy, "/", C_DIM, C_HEADER);
        tx += 8;
        tm_text(buf, cw, ch, tx, fy, tbuf, C_TEXT, C_HEADER);
        tx += str_len(tbuf) * 8;
        tm_text(buf, cw, ch, tx, fy, " KB", C_DIM, C_HEADER);
    }
}
