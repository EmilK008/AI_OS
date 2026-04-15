/* ===========================================================================
 * Paint - Simple pixel drawing app with brush, colors, and file save/load
 * =========================================================================== */
#include "paint.h"
#include "window.h"
#include "framebuffer.h"
#include "font8x16.h"
#include "fs.h"
#include "string.h"
#include "keyboard.h"
#include "timer.h"
#include "memory.h"

#define PT_W          400
#define PT_H          320
#define TOOLBAR_H     42
#define CANVAS_PAD    10
#define CANVAS_X      CANVAS_PAD
#define CANVAS_Y      (TOOLBAR_H + 2)
#define CANVAS_W      (PT_W - CANVAS_PAD * 2)   /* 380 */
#define CANVAS_H      (PT_H - TOOLBAR_H - CANVAS_PAD - 2) /* 266 */
#define CANVAS_BG     COLOR_WHITE

/* Palette */
#define PAL_COUNT    16
#define PAL_ROW      8
#define SWATCH_SZ    14
#define SWATCH_GAP   2

/* Brush sizes */
#define BRUSH_COUNT  3
static const int brush_sizes[BRUSH_COUNT] = { 1, 3, 5 };

/* Toolbar buttons */
#define BTN_W   32
#define BTN_H   18
#define BTN_GAP 4

/* Toolbar row Y positions */
#define ROW1_Y  4
#define ROW2_Y  22

/* Modes */
#define MODE_DRAW   0
#define MODE_SAVE   1
#define MODE_OPEN   2

/* Colors */
#define C_TOOLBAR  COLOR_RGB(50, 50, 62)
#define C_BTN      COLOR_RGB(60, 60, 78)
#define C_BTN_ACT  COLOR_RGB(40, 120, 60)
#define C_BTN_DIS  COLOR_RGB(45, 45, 55)
#define C_BTN_TEXT COLOR_WHITE
#define C_BORDER   COLOR_RGB(80, 80, 100)

static const color_t palette[PAL_COUNT] = {
    COLOR_RGB(0, 0, 0),        COLOR_RGB(255, 255, 255),
    COLOR_RGB(180, 0, 0),      COLOR_RGB(255, 60, 60),
    COLOR_RGB(220, 120, 0),    COLOR_RGB(255, 200, 0),
    COLOR_RGB(0, 160, 0),      COLOR_RGB(0, 220, 0),
    COLOR_RGB(0, 130, 180),    COLOR_RGB(0, 180, 255),
    COLOR_RGB(0, 0, 180),      COLOR_RGB(80, 80, 255),
    COLOR_RGB(130, 0, 180),    COLOR_RGB(200, 80, 255),
    COLOR_RGB(128, 128, 128),  COLOR_RGB(200, 200, 200),
};

/* X offset where brush/action buttons start (after 8 swatches) */
#define TOOLS_X  (4 + PAL_ROW * (SWATCH_SZ + SWATCH_GAP) + 8)

static int win_id = -1;
static int mode = MODE_DRAW;

/* Canvas — stored as palette indices to enable compact saving */
static uint8_t canvas[CANVAS_H][CANVAS_W];
static int current_color = 0;  /* palette index */
static int brush_idx = 1;      /* default medium brush (3px) */
static bool eraser_on = false;
static bool drawing = false;
static int last_mx = -1, last_my = -1;

/* File dialog */
static char input_buf[32];
static int  input_len = 0;

/* --- Undo/Redo history --- */
#define HISTORY_MAX 8
static uint8_t *history[HISTORY_MAX];
static int hist_count = 0;   /* number of valid snapshots */
static int hist_pos = -1;    /* current position (-1 = no history) */
static bool hist_ready = false;

static void history_init(void) {
    for (int i = 0; i < HISTORY_MAX; i++) {
        history[i] = (uint8_t *)kmalloc(CANVAS_W * CANVAS_H);
    }
    hist_count = 0;
    hist_pos = -1;
    hist_ready = true;
}

static void history_push(void) {
    if (!hist_ready) return;
    /* Discard any redo states ahead of current position */
    hist_pos++;
    if (hist_pos >= HISTORY_MAX) {
        /* Shift everything left to make room */
        uint8_t *tmp = history[0];
        for (int i = 0; i < HISTORY_MAX - 1; i++)
            history[i] = history[i + 1];
        history[HISTORY_MAX - 1] = tmp;
        hist_pos = HISTORY_MAX - 1;
    }
    hist_count = hist_pos + 1;
    mem_copy(history[hist_pos], canvas, CANVAS_W * CANVAS_H);
}

static void history_undo(void) {
    if (!hist_ready || hist_pos <= 0) return;
    hist_pos--;
    mem_copy(canvas, history[hist_pos], CANVAS_W * CANVAS_H);
}

static void history_redo(void) {
    if (!hist_ready || hist_pos >= hist_count - 1) return;
    hist_pos++;
    mem_copy(canvas, history[hist_pos], CANVAS_W * CANVAS_H);
}

/* --- Drawing helpers --- */

static void pt_rect(color_t *buf, int cw, int ch, int x, int y, int w, int h, color_t c) {
    for (int py = y; py < y + h && py < ch; py++) {
        if (py < 0) continue;
        for (int px = x; px < x + w && px < cw; px++)
            if (px >= 0) buf[py * cw + px] = c;
    }
}

static void pt_text(color_t *buf, int cw, int ch, int px, int py,
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

/* Paint a brush dot on the canvas at (cx, cy) in canvas coords */
static void paint_dot(int cx, int cy) {
    int r = brush_sizes[brush_idx] / 2;
    uint8_t col = eraser_on ? 1 : (uint8_t)current_color; /* 1 = white (eraser) */
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            int px = cx + dx, py = cy + dy;
            if (px >= 0 && px < CANVAS_W && py >= 0 && py < CANVAS_H) {
                /* Circle shape for larger brushes */
                if (r > 0 && dx * dx + dy * dy > r * r) continue;
                canvas[py][px] = col;
            }
        }
    }
}

/* Bresenham line between two canvas points for smooth strokes */
static void paint_line(int x0, int y0, int x1, int y1) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int sx = (dx > 0) ? 1 : -1;
    int sy = (dy > 0) ? 1 : -1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;

    int err = dx - dy;
    while (1) {
        paint_dot(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = err * 2;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

static void clear_canvas(void) {
    mem_set(canvas, 1, sizeof(canvas)); /* 1 = white palette index */
}

/* --- Save/Load with RLE compression --- */

static void save_pic(const char *name) {
    /* Format: "PIC" (3) + w (2) + h (2) = 7 byte header
     * Then RLE: pairs of (count, palette_index), count 1-255 */
    char buf[MAX_FILE_DATA];
    int pos = 0;

    /* Header */
    buf[pos++] = 'P'; buf[pos++] = 'I'; buf[pos++] = 'C';
    buf[pos++] = (char)(CANVAS_W & 0xFF);
    buf[pos++] = (char)((CANVAS_W >> 8) & 0xFF);
    buf[pos++] = (char)(CANVAS_H & 0xFF);
    buf[pos++] = (char)((CANVAS_H >> 8) & 0xFF);

    /* RLE encode row by row */
    for (int y = 0; y < CANVAS_H; y++) {
        int x = 0;
        while (x < CANVAS_W) {
            uint8_t val = canvas[y][x];
            int run = 1;
            while (x + run < CANVAS_W && canvas[y][x + run] == val && run < 255)
                run++;
            if (pos + 2 > MAX_FILE_DATA - 1) goto save_done;
            buf[pos++] = (char)run;
            buf[pos++] = (char)val;
            x += run;
        }
    }

save_done:;
    int idx = fs_find(name);
    if (idx < 0) idx = fs_create(name, FS_FILE);
    if (idx >= 0) fs_write_file(idx, buf, (uint32_t)pos);
}

static void load_pic(const char *name) {
    int idx = fs_find(name);
    if (idx < 0) return;
    struct fs_node *f = fs_get_node(idx);
    if (!f || f->type != FS_FILE || f->size < 7) return;

    const char *d = f->data;
    if (d[0] != 'P' || d[1] != 'I' || d[2] != 'C') return;

    int w = (uint8_t)d[3] | ((uint8_t)d[4] << 8);
    int h = (uint8_t)d[5] | ((uint8_t)d[6] << 8);
    if (w != CANVAS_W || h != CANVAS_H) {
        clear_canvas();
        return;
    }

    /* RLE decode */
    int pos = 7;
    int size = (int)f->size;
    clear_canvas();
    for (int y = 0; y < CANVAS_H; y++) {
        int x = 0;
        while (x < CANVAS_W && pos + 1 < size) {
            int run = (uint8_t)d[pos++];
            uint8_t val = (uint8_t)d[pos++];
            if (val >= PAL_COUNT) val = 0;
            for (int i = 0; i < run && x < CANVAS_W; i++)
                canvas[y][x++] = val;
        }
    }
    history_push();
}

/* --- Event handler --- */

static void paint_on_event(struct window *win, struct gui_event *evt) {
    if (evt->type == EVT_KEY_PRESS) {
        uint8_t k = evt->key;

        /* File dialog */
        if (mode == MODE_SAVE || mode == MODE_OPEN) {
            if (k == 0x1B) { mode = MODE_DRAW; return; }
            if (k == '\n' && input_len > 0) {
                input_buf[input_len] = '\0';
                if (mode == MODE_SAVE) save_pic(input_buf);
                else load_pic(input_buf);
                mode = MODE_DRAW;
                return;
            }
            if (k == '\b') { if (input_len > 0) input_len--; return; }
            if (input_len < 30 && k >= 0x20 && k < 0x7F)
                input_buf[input_len++] = (char)k;
            return;
        }

        /* Keyboard shortcuts */
        if (k == 'e' || k == 'E') { eraser_on = !eraser_on; return; }
        if (k == 14) { clear_canvas(); history_push(); return; } /* Ctrl+N */
        if (k == 19) { input_len = 0; mode = MODE_SAVE; return; } /* Ctrl+S */
        if (k == 26) { history_undo(); return; } /* Ctrl+Z - Undo */
        if (k == 25) { history_redo(); return; } /* Ctrl+Y - Redo */
        if (k == '[' || k == '-') {
            if (brush_idx > 0) brush_idx--;
            return;
        }
        if (k == ']' || k == '+' || k == '=') {
            if (brush_idx < BRUSH_COUNT - 1) brush_idx++;
            return;
        }
        /* Number keys 1-9 for quick color select */
        if (k >= '1' && k <= '9') {
            int ci = k - '1';
            if (ci < PAL_COUNT) { current_color = ci; eraser_on = false; }
            return;
        }
        if (k == '0') { current_color = 9; eraser_on = false; return; }
        return;
    }

    int mx = evt->mouse_x - (win->x + BORDER_WIDTH);
    int my = evt->mouse_y - (win->y + TITLEBAR_HEIGHT);

    if (evt->type == EVT_MOUSE_DOWN) {
        /* Row 1 toolbar clicks (y = ROW1_Y .. ROW1_Y+BTN_H) */
        if (my >= ROW1_Y && my < ROW1_Y + BTN_H) {
            /* Color palette row 1: swatches 0-7 */
            int pal_x = 4;
            for (int i = 0; i < PAL_ROW; i++) {
                int sx = pal_x + i * (SWATCH_SZ + SWATCH_GAP);
                if (mx >= sx && mx < sx + SWATCH_SZ) {
                    current_color = i;
                    eraser_on = false;
                    return;
                }
            }

            /* Brush size buttons */
            int bx = TOOLS_X;
            for (int i = 0; i < BRUSH_COUNT; i++) {
                int btn_x = bx + i * (BTN_W + BTN_GAP);
                if (mx >= btn_x && mx < btn_x + BTN_W) {
                    brush_idx = i;
                    return;
                }
            }

            /* Action buttons: Eraser, New, Save, Open */
            int ax = TOOLS_X + BRUSH_COUNT * (BTN_W + BTN_GAP) + 8;
            /* Eraser */
            if (mx >= ax && mx < ax + BTN_W) { eraser_on = !eraser_on; return; }
            ax += BTN_W + BTN_GAP;
            /* New */
            if (mx >= ax && mx < ax + BTN_W) { clear_canvas(); history_push(); return; }
            ax += BTN_W + BTN_GAP;
            /* Save */
            if (mx >= ax && mx < ax + BTN_W) { input_len = 0; mode = MODE_SAVE; return; }
            ax += BTN_W + BTN_GAP;
            /* Open */
            if (mx >= ax && mx < ax + BTN_W) { input_len = 0; mode = MODE_OPEN; return; }
        }

        /* Row 2 toolbar clicks (y = ROW2_Y .. ROW2_Y+BTN_H) */
        if (my >= ROW2_Y && my < ROW2_Y + BTN_H) {
            /* Color palette row 2: swatches 8-15 */
            int pal_x = 4;
            for (int i = 0; i < PAL_ROW; i++) {
                int sx = pal_x + i * (SWATCH_SZ + SWATCH_GAP);
                if (mx >= sx && mx < sx + SWATCH_SZ) {
                    current_color = PAL_ROW + i;
                    eraser_on = false;
                    return;
                }
            }

            /* Undo / Redo buttons */
            int ux = TOOLS_X;
            if (mx >= ux && mx < ux + BTN_W) { history_undo(); return; }
            ux += BTN_W + BTN_GAP;
            if (mx >= ux && mx < ux + BTN_W) { history_redo(); return; }
        }

        /* Canvas click — start drawing */
        int cx = mx - CANVAS_X;
        int cy = my - CANVAS_Y;
        if (cx >= 0 && cx < CANVAS_W && cy >= 0 && cy < CANVAS_H) {
            drawing = true;
            paint_dot(cx, cy);
            last_mx = cx;
            last_my = cy;
        }
        return;
    }

    if (evt->type == EVT_MOUSE_MOVE) {
        if (drawing) {
            int cx = mx - CANVAS_X;
            int cy = my - CANVAS_Y;
            /* Clamp to canvas */
            if (cx < 0) cx = 0;
            if (cx >= CANVAS_W) cx = CANVAS_W - 1;
            if (cy < 0) cy = 0;
            if (cy >= CANVAS_H) cy = CANVAS_H - 1;

            if (last_mx >= 0 && last_my >= 0) {
                paint_line(last_mx, last_my, cx, cy);
            } else {
                paint_dot(cx, cy);
            }
            last_mx = cx;
            last_my = cy;
        }
        return;
    }

    if (evt->type == EVT_MOUSE_UP) {
        if (drawing) {
            drawing = false;
            last_mx = -1;
            last_my = -1;
            history_push();
        }
        return;
    }
}

void paint_create(void) {
    if (win_id >= 0) {
        struct window *w = wm_get_window(win_id);
        if (w && w->alive) { wm_focus_window(win_id); return; }
    }
    win_id = wm_create_window("Paint", 120, 40, PT_W, PT_H,
                               paint_on_event, NULL);
    clear_canvas();
    current_color = 0;
    brush_idx = 1;
    eraser_on = false;
    drawing = false;
    mode = MODE_DRAW;
    if (!hist_ready) history_init();
    hist_count = 0;
    hist_pos = -1;
    history_push(); /* save initial blank canvas */
}

bool paint_is_alive(void) {
    if (win_id < 0) return false;
    struct window *w = wm_get_window(win_id);
    return (w && w->alive);
}

void paint_render(void) {
    if (win_id < 0) return;
    struct window *win = wm_get_window(win_id);
    if (!win || !win->alive || !win->content) { win_id = -1; return; }

    int cw = win->content_w;
    int ch = win->content_h;
    color_t *buf = win->content;

    /* Background */
    for (int i = 0; i < cw * ch; i++) buf[i] = COLOR_RGB(45, 45, 55);

    /* Toolbar background (both rows) */
    pt_rect(buf, cw, ch, 0, 0, cw, TOOLBAR_H, C_TOOLBAR);

    /* === Row 1 === */

    /* Color palette row 1: swatches 0-7 */
    int pal_x = 4;
    for (int i = 0; i < PAL_ROW; i++) {
        int sx = pal_x + i * (SWATCH_SZ + SWATCH_GAP);
        pt_rect(buf, cw, ch, sx, ROW1_Y, SWATCH_SZ, SWATCH_SZ, palette[i]);
        if (i == current_color && !eraser_on) {
            pt_rect(buf, cw, ch, sx - 1, ROW1_Y - 1, SWATCH_SZ + 2, 1, COLOR_WHITE);
            pt_rect(buf, cw, ch, sx - 1, ROW1_Y + SWATCH_SZ, SWATCH_SZ + 2, 1, COLOR_WHITE);
            pt_rect(buf, cw, ch, sx - 1, ROW1_Y - 1, 1, SWATCH_SZ + 2, COLOR_WHITE);
            pt_rect(buf, cw, ch, sx + SWATCH_SZ, ROW1_Y - 1, 1, SWATCH_SZ + 2, COLOR_WHITE);
        }
    }

    /* Brush size buttons */
    int bx = TOOLS_X;
    for (int i = 0; i < BRUSH_COUNT; i++) {
        int btn_x = bx + i * (BTN_W + BTN_GAP);
        color_t bc = (i == brush_idx) ? C_BTN_ACT : C_BTN;
        pt_rect(buf, cw, ch, btn_x, ROW1_Y, BTN_W, BTN_H, bc);
        int dot_r = brush_sizes[i] / 2;
        int dot_cx = btn_x + BTN_W / 2;
        int dot_cy = ROW1_Y + BTN_H / 2;
        for (int dy = -dot_r; dy <= dot_r; dy++)
            for (int dx = -dot_r; dx <= dot_r; dx++)
                if (dot_r == 0 || dx*dx + dy*dy <= dot_r*dot_r)
                    if (dot_cx+dx >= 0 && dot_cx+dx < cw && dot_cy+dy >= 0 && dot_cy+dy < ch)
                        buf[(dot_cy+dy)*cw + (dot_cx+dx)] = C_BTN_TEXT;
    }

    /* Action buttons: Ers, New, Sav, Opn */
    int ax = TOOLS_X + BRUSH_COUNT * (BTN_W + BTN_GAP) + 8;
    static const char *abtn[] = { "Ers", "New", "Sav", "Opn" };
    for (int i = 0; i < 4; i++) {
        int btn_x = ax + i * (BTN_W + BTN_GAP);
        color_t bc = (i == 0 && eraser_on) ? C_BTN_ACT : C_BTN;
        pt_rect(buf, cw, ch, btn_x, ROW1_Y, BTN_W, BTN_H, bc);
        int tlen = str_len(abtn[i]);
        int tx = btn_x + (BTN_W - tlen * 8) / 2;
        pt_text(buf, cw, ch, tx, ROW1_Y + 1, abtn[i], C_BTN_TEXT, bc);
    }

    /* === Row 2 === */

    /* Color palette row 2: swatches 8-15 */
    for (int i = 0; i < PAL_ROW; i++) {
        int sx = pal_x + i * (SWATCH_SZ + SWATCH_GAP);
        int ci = PAL_ROW + i;
        pt_rect(buf, cw, ch, sx, ROW2_Y, SWATCH_SZ, SWATCH_SZ, palette[ci]);
        if (ci == current_color && !eraser_on) {
            pt_rect(buf, cw, ch, sx - 1, ROW2_Y - 1, SWATCH_SZ + 2, 1, COLOR_WHITE);
            pt_rect(buf, cw, ch, sx - 1, ROW2_Y + SWATCH_SZ, SWATCH_SZ + 2, 1, COLOR_WHITE);
            pt_rect(buf, cw, ch, sx - 1, ROW2_Y - 1, 1, SWATCH_SZ + 2, COLOR_WHITE);
            pt_rect(buf, cw, ch, sx + SWATCH_SZ, ROW2_Y - 1, 1, SWATCH_SZ + 2, COLOR_WHITE);
        }
    }

    /* Undo / Redo buttons */
    int ux = TOOLS_X;
    {
        bool can_undo = (hist_pos > 0);
        color_t bc = can_undo ? C_BTN : C_BTN_DIS;
        pt_rect(buf, cw, ch, ux, ROW2_Y, BTN_W, BTN_H, bc);
        color_t tc = can_undo ? C_BTN_TEXT : COLOR_RGB(100, 100, 100);
        pt_text(buf, cw, ch, ux + 4, ROW2_Y + 1, "Und", tc, bc);
    }
    ux += BTN_W + BTN_GAP;
    {
        bool can_redo = (hist_pos < hist_count - 1);
        color_t bc = can_redo ? C_BTN : C_BTN_DIS;
        pt_rect(buf, cw, ch, ux, ROW2_Y, BTN_W, BTN_H, bc);
        color_t tc = can_redo ? C_BTN_TEXT : COLOR_RGB(100, 100, 100);
        pt_text(buf, cw, ch, ux + 4, ROW2_Y + 1, "Red", tc, bc);
    }

    /* Canvas border */
    pt_rect(buf, cw, ch, CANVAS_X - 1, CANVAS_Y - 1,
            CANVAS_W + 2, CANVAS_H + 2, C_BORDER);

    /* Canvas content — blit palette-indexed pixels */
    for (int y = 0; y < CANVAS_H; y++) {
        int dy = CANVAS_Y + y;
        if (dy >= ch) break;
        for (int x = 0; x < CANVAS_W; x++) {
            int dx = CANVAS_X + x;
            if (dx >= cw) break;
            uint8_t pi = canvas[y][x];
            if (pi >= PAL_COUNT) pi = 0;
            buf[dy * cw + dx] = palette[pi];
        }
    }

    /* Status bar / file dialog (overlaid on bottom of canvas) */
    if (mode == MODE_SAVE || mode == MODE_OPEN) {
        int sy = PT_H - 22;
        pt_rect(buf, cw, ch, 0, sy, PT_W, 22, C_TOOLBAR);
        const char *prompt = (mode == MODE_SAVE) ? "Save as: " : "Open: ";
        pt_text(buf, cw, ch, 4, sy + 3, prompt, COLOR_YELLOW, C_TOOLBAR);
        input_buf[input_len] = '\0';
        int px = 4 + str_len(prompt) * 8;
        pt_text(buf, cw, ch, px, sy + 3, input_buf, COLOR_WHITE, C_TOOLBAR);
        if ((timer_get_ticks() / 40) & 1)
            pt_rect(buf, cw, ch, px + input_len * 8, sy + 3, 8, 16,
                    COLOR_RGB(100, 200, 255));
    }
}
