/* ===========================================================================
 * Calculator - Windowed GUI calculator app
 * =========================================================================== */
#include "calculator.h"
#include "window.h"
#include "framebuffer.h"
#include "font8x16.h"
#include "string.h"
#include "keyboard.h"

#define CALC_W      240
#define CALC_H      300

#define DISPLAY_H   50
#define DISPLAY_PAD 8

#define BTN_ROWS    5
#define BTN_COLS    4
#define BTN_GAP     4
#define BTN_AREA_X  BTN_GAP
#define BTN_AREA_Y  (DISPLAY_H + BTN_GAP)
#define BTN_W       ((CALC_W - BTN_GAP * (BTN_COLS + 1)) / BTN_COLS)  /* ~54 */
#define BTN_H       ((CALC_H - BTN_AREA_Y - BTN_GAP * (BTN_ROWS + 1)) / BTN_ROWS) /* ~44 */

/* Colors */
#define C_BG        COLOR_RGB(30, 30, 38)
#define C_DISP_BG   COLOR_RGB(20, 20, 28)
#define C_DISP_TEXT COLOR_WHITE
#define C_BTN_NUM   COLOR_RGB(55, 55, 68)
#define C_BTN_OP    COLOR_RGB(50, 100, 160)
#define C_BTN_FUNC  COLOR_RGB(70, 55, 55)
#define C_BTN_EQ    COLOR_RGB(40, 130, 70)
#define C_BTN_TEXT  COLOR_WHITE

static int win_id = -1;

/* Calculator state */
static long accumulator;
static long current;
static char display[20];
static int  display_len;
static char pending_op;
static bool new_input;
static bool has_error;

/* Button layout: label, type (0=num, 1=op, 2=func, 3=eq) */
static const char btn_labels[BTN_ROWS][BTN_COLS] = {
    { 'C', 'N', '%', '/' },  /* N = negate (+/-) */
    { '7', '8', '9', '*' },
    { '4', '5', '6', '-' },
    { '1', '2', '3', '+' },
    { '0', ' ', '.', '=' },  /* row 4: 0 takes 2 cols, . and = */
};

static const int btn_types[BTN_ROWS][BTN_COLS] = {
    { 2, 2, 1, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 1 },
    { 0, 0, 0, 3 },
};

static void calc_rect(color_t *buf, int cw, int ch, int x, int y, int w, int h, color_t c) {
    for (int py = y; py < y + h && py < ch; py++) {
        if (py < 0) continue;
        for (int px = x; px < x + w && px < cw; px++) {
            if (px >= 0) buf[py * cw + px] = c;
        }
    }
}

static void calc_text(color_t *buf, int cw, int ch, int px, int py,
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

static void long_to_str(long n, char *buf) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    bool neg = false;
    if (n < 0) { neg = true; n = -n; }
    char tmp[16];
    int i = 0;
    while (n > 0 && i < 15) { tmp[i++] = '0' + (int)(n % 10); n /= 10; }
    int j = 0;
    if (neg) buf[j++] = '-';
    while (--i >= 0) buf[j++] = tmp[i];
    buf[j] = '\0';
}

static void update_display(long val) {
    long_to_str(val, display);
    display_len = str_len(display);
}

static void calc_reset(void) {
    accumulator = 0;
    current = 0;
    pending_op = 0;
    new_input = true;
    has_error = false;
    display[0] = '0';
    display[1] = '\0';
    display_len = 1;
}

static long apply_op(long a, char op, long b) {
    switch (op) {
        case '+': return a + b;
        case '-': return a - b;
        case '*': return a * b;
        case '/': if (b == 0) { has_error = true; return 0; } return a / b;
        case '%': if (b == 0) { has_error = true; return 0; } return a % b;
    }
    return b;
}

static void input_digit(char d) {
    if (has_error) calc_reset();
    if (new_input) {
        display[0] = d;
        display[1] = '\0';
        display_len = 1;
        new_input = false;
    } else {
        if (display_len < 15) {
            /* Don't allow leading zeros */
            if (display_len == 1 && display[0] == '0' && d == '0') return;
            if (display_len == 1 && display[0] == '0') {
                display[0] = d;
            } else {
                display[display_len++] = d;
                display[display_len] = '\0';
            }
        }
    }
    /* Parse display to current */
    current = 0;
    bool neg = false;
    int start = 0;
    if (display[0] == '-') { neg = true; start = 1; }
    for (int i = start; display[i]; i++) {
        if (display[i] >= '0' && display[i] <= '9')
            current = current * 10 + (display[i] - '0');
    }
    if (neg) current = -current;
}

static void input_operator(char op) {
    if (has_error) calc_reset();
    if (pending_op && !new_input) {
        accumulator = apply_op(accumulator, pending_op, current);
        if (has_error) {
            display[0] = 'E'; display[1] = 'r'; display[2] = 'r';
            display[3] = '\0'; display_len = 3;
            return;
        }
        update_display(accumulator);
    } else {
        accumulator = current;
    }
    pending_op = op;
    new_input = true;
}

static void input_equals(void) {
    if (has_error) { calc_reset(); return; }
    if (pending_op) {
        accumulator = apply_op(accumulator, pending_op, current);
        if (has_error) {
            display[0] = 'E'; display[1] = 'r'; display[2] = 'r';
            display[3] = '\0'; display_len = 3;
            return;
        }
        current = accumulator;
        update_display(accumulator);
        pending_op = 0;
    }
    new_input = true;
}

static void input_negate(void) {
    if (has_error) return;
    current = -current;
    update_display(current);
    new_input = false;
}

static void input_backspace(void) {
    if (has_error) { calc_reset(); return; }
    if (new_input) return;
    if (display_len > 1) {
        display_len--;
        display[display_len] = '\0';
    } else {
        display[0] = '0';
        display[1] = '\0';
    }
    /* Reparse */
    current = 0;
    bool neg = false;
    int start = 0;
    if (display[0] == '-') { neg = true; start = 1; }
    for (int i = start; display[i]; i++) {
        if (display[i] >= '0' && display[i] <= '9')
            current = current * 10 + (display[i] - '0');
    }
    if (neg) current = -current;
}

static void handle_btn(int row, int col) {
    char label = btn_labels[row][col];

    if (label == 'C') { calc_reset(); return; }
    if (label == 'N') { input_negate(); return; }
    if (label == '=') { input_equals(); return; }
    if (label == '+' || label == '-' || label == '*' || label == '/' || label == '%') {
        input_operator(label);
        return;
    }
    if (label >= '0' && label <= '9') {
        input_digit(label);
        return;
    }
}

static void calc_on_event(struct window *win, struct gui_event *evt) {
    (void)win;

    if (evt->type == EVT_KEY_PRESS) {
        uint8_t k = evt->key;
        if (k >= '0' && k <= '9') { input_digit((char)k); return; }
        if (k == '+' || k == '-' || k == '*' || k == '/' || k == '%') {
            input_operator((char)k); return;
        }
        if (k == '\n' || k == '=') { input_equals(); return; }
        if (k == '\b') { input_backspace(); return; }
        if (k == 0x1B || k == 'c' || k == 'C') { calc_reset(); return; }
        return;
    }

    if (evt->type == EVT_MOUSE_DOWN) {
        int mx = evt->mouse_x - (win->x + BORDER_WIDTH);
        int my = evt->mouse_y - (win->y + TITLEBAR_HEIGHT);

        /* Check button hits */
        for (int row = 0; row < BTN_ROWS; row++) {
            for (int col = 0; col < BTN_COLS; col++) {
                /* Row 4, col 0: wide "0" button */
                int bx, by, bw, bh;
                if (row == 4 && col == 0) {
                    bx = BTN_AREA_X + BTN_GAP;
                    by = BTN_AREA_Y + row * (BTN_H + BTN_GAP) + BTN_GAP;
                    bw = BTN_W * 2 + BTN_GAP;
                    bh = BTN_H;
                } else if (row == 4 && col == 1) {
                    continue; /* Part of '0' button */
                } else {
                    bx = BTN_AREA_X + col * (BTN_W + BTN_GAP) + BTN_GAP;
                    by = BTN_AREA_Y + row * (BTN_H + BTN_GAP) + BTN_GAP;
                    bw = BTN_W;
                    bh = BTN_H;
                }

                if (mx >= bx && mx < bx + bw && my >= by && my < by + bh) {
                    handle_btn(row, col);
                    return;
                }
            }
        }
    }
}

void calculator_create(void) {
    if (win_id >= 0) {
        struct window *w = wm_get_window(win_id);
        if (w && w->alive && w->on_event == calc_on_event) { wm_focus_window(win_id); return; }
    }
    win_id = wm_create_window("Calculator", 200, 80, CALC_W, CALC_H,
                               calc_on_event, NULL);
    calc_reset();
}

bool calculator_is_alive(void) {
    if (win_id < 0) return false;
    struct window *w = wm_get_window(win_id);
    return (w && w->alive);
}

void calculator_render(void) {
    if (win_id < 0) return;
    struct window *win = wm_get_window(win_id);
    if (!win || !win->alive || !win->content || win->on_event != calc_on_event) { win_id = -1; return; }

    int cw = win->content_w;
    int ch = win->content_h;
    color_t *buf = win->content;

    /* Background */
    for (int i = 0; i < cw * ch; i++) buf[i] = C_BG;

    /* Display area */
    calc_rect(buf, cw, ch, DISPLAY_PAD, DISPLAY_PAD,
              CALC_W - DISPLAY_PAD * 2, DISPLAY_H - DISPLAY_PAD, C_DISP_BG);

    /* Display text - right aligned */
    const char *disp = has_error ? "Error" : display;
    int tlen = str_len(disp);
    int tx = CALC_W - DISPLAY_PAD - 8 - tlen * 8;
    int ty = DISPLAY_PAD + (DISPLAY_H - DISPLAY_PAD - 16) / 2;
    calc_text(buf, cw, ch, tx, ty, disp, C_DISP_TEXT, C_DISP_BG);

    /* Buttons */
    for (int row = 0; row < BTN_ROWS; row++) {
        for (int col = 0; col < BTN_COLS; col++) {
            int bx, by, bw, bh;
            char label_str[4];

            /* Row 4, col 0: wide "0" button */
            if (row == 4 && col == 0) {
                bx = BTN_AREA_X + BTN_GAP;
                by = BTN_AREA_Y + row * (BTN_H + BTN_GAP) + BTN_GAP;
                bw = BTN_W * 2 + BTN_GAP;
                bh = BTN_H;
            } else if (row == 4 && col == 1) {
                continue; /* Part of '0' button */
            } else {
                bx = BTN_AREA_X + col * (BTN_W + BTN_GAP) + BTN_GAP;
                by = BTN_AREA_Y + row * (BTN_H + BTN_GAP) + BTN_GAP;
                bw = BTN_W;
                bh = BTN_H;
            }

            /* Button color based on type */
            color_t btn_color;
            int type = btn_types[row][col];
            switch (type) {
                case 1: btn_color = C_BTN_OP;   break;
                case 2: btn_color = C_BTN_FUNC; break;
                case 3: btn_color = C_BTN_EQ;   break;
                default: btn_color = C_BTN_NUM; break;
            }
            calc_rect(buf, cw, ch, bx, by, bw, bh, btn_color);

            /* Button label */
            char c = btn_labels[row][col];
            if (c == 'N') {
                label_str[0] = '+'; label_str[1] = '/'; label_str[2] = '-'; label_str[3] = '\0';
            } else if (c == ' ') {
                continue;
            } else {
                label_str[0] = c; label_str[1] = '\0';
            }
            int llen = str_len(label_str);
            int lx = bx + (bw - llen * 8) / 2;
            int ly = by + (bh - 16) / 2;
            calc_text(buf, cw, ch, lx, ly, label_str, C_BTN_TEXT, btn_color);
        }
    }
}
