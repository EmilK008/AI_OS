/* ===========================================================================
 * Browser - HTML file viewer with CSS support and tabbed browsing
 * Renders HTML tags with inline style="", <style> blocks, and .class selectors
 * =========================================================================== */
#include "browser.h"
#include "window.h"
#include "framebuffer.h"
#include "font8x16.h"
#include "fs.h"
#include "string.h"
#include "keyboard.h"
#include "timer.h"

#define BRW_W       500
#define BRW_H       360
#define TOOLBAR_H   26
#define TAB_BAR_H   18
#define STATUS_H    18
#define CONTENT_Y   (TOOLBAR_H + TAB_BAR_H)
#define CONTENT_H   (BRW_H - TOOLBAR_H - TAB_BAR_H - STATUS_H)

/* Address bar */
#define ADDR_X      74
#define ADDR_W      (BRW_W - ADDR_X - 40)
#define ADDR_MAX    30

/* Default colors */
#define C_BG        COLOR_RGB(245, 245, 240)
#define C_TOOLBAR   COLOR_RGB(220, 222, 228)
#define C_ADDR_BG   COLOR_WHITE
#define C_ADDR_FG   COLOR_RGB(30, 30, 30)
#define C_STATUS_BG COLOR_RGB(230, 230, 235)
#define C_STATUS_FG COLOR_RGB(80, 80, 80)
#define C_TEXT       COLOR_RGB(30, 30, 30)
#define C_H1         COLOR_RGB(20, 20, 80)
#define C_H2         COLOR_RGB(30, 60, 120)
#define C_H3         COLOR_RGB(40, 80, 130)
#define C_BOLD       COLOR_RGB(0, 0, 0)
#define C_ITALIC     COLOR_RGB(100, 60, 120)
#define C_LINK       COLOR_RGB(0, 80, 200)
#define C_HR         COLOR_RGB(180, 180, 180)
#define C_BULLET     COLOR_RGB(80, 80, 80)
#define C_BTN_BG    COLOR_RGB(200, 200, 208)
#define C_BTN_FG    COLOR_RGB(40, 40, 40)
#define C_NONE      0xFF000000  /* sentinel: no color set */
#define C_TAB_BG    COLOR_RGB(200, 202, 210)
#define C_TAB_ACT   COLOR_RGB(245, 245, 240)
#define C_TAB_FG    COLOR_RGB(60, 60, 60)
#define C_TAB_CLOSE COLOR_RGB(150, 50, 50)

/* Page content buffer */
#define PAGE_BUF_SIZE  8192

/* Navigation history */
#define HISTORY_MAX 16

/* ===========================================================================
 * Image Support (PIC format)
 * =========================================================================== */

#define PIC_PAL_COUNT 16
static const color_t brw_palette[PIC_PAL_COUNT] = {
    COLOR_RGB(0, 0, 0),        COLOR_RGB(255, 255, 255),
    COLOR_RGB(180, 0, 0),      COLOR_RGB(255, 60, 60),
    COLOR_RGB(220, 120, 0),    COLOR_RGB(255, 200, 0),
    COLOR_RGB(0, 160, 0),      COLOR_RGB(0, 220, 0),
    COLOR_RGB(0, 130, 180),    COLOR_RGB(0, 180, 255),
    COLOR_RGB(0, 0, 180),      COLOR_RGB(80, 80, 255),
    COLOR_RGB(130, 0, 180),    COLOR_RGB(200, 80, 255),
    COLOR_RGB(128, 128, 128),  COLOR_RGB(200, 200, 200),
};

#define IMG_MAX_W 200
#define IMG_MAX_H 150
static uint8_t img_decode_buf[IMG_MAX_W * IMG_MAX_H];

static bool pic_decode(const char *data, int size, int *out_w, int *out_h) {
    if (size < 7) return false;
    if (data[0] != 'P' || data[1] != 'I' || data[2] != 'C') return false;
    int w = (uint8_t)data[3] | ((uint8_t)data[4] << 8);
    int h = (uint8_t)data[5] | ((uint8_t)data[6] << 8);
    if (w <= 0 || h <= 0) return false;
    if (w > IMG_MAX_W) w = IMG_MAX_W;
    if (h > IMG_MAX_H) h = IMG_MAX_H;
    mem_set(img_decode_buf, 1, (uint32_t)(w * h));
    int pos = 7;
    for (int y = 0; y < h; y++) {
        int x = 0;
        while (x < w && pos + 1 < size) {
            int run = (uint8_t)data[pos++];
            uint8_t val = (uint8_t)data[pos++];
            if (val >= PIC_PAL_COUNT) val = 0;
            for (int i = 0; i < run && x < w; i++)
                img_decode_buf[y * w + x++] = val;
        }
    }
    *out_w = w;
    *out_h = h;
    return true;
}

static void brw_blit_pic(color_t *buf, int cw, int ch __attribute__((unused)),
                          int dx, int dy, int img_w, int img_h, int scroll_y) {
    for (int y = 0; y < img_h; y++) {
        int screen_y = CONTENT_Y + dy + y - scroll_y;
        if (screen_y < CONTENT_Y) continue;
        if (screen_y >= CONTENT_Y + CONTENT_H) break;
        for (int x = 0; x < img_w; x++) {
            int screen_x = dx + x;
            if (screen_x < 0 || screen_x >= cw) continue;
            uint8_t idx = img_decode_buf[y * img_w + x];
            if (idx < PIC_PAL_COUNT)
                buf[screen_y * cw + screen_x] = brw_palette[idx];
        }
    }
}

/* Clickable link regions */
#define MAX_LINKS 32
struct link_region {
    int x, y, w, h;
    char href[ADDR_MAX + 1];
};

/* ===========================================================================
 * Form Elements
 * =========================================================================== */

#define MAX_FORM_FIELDS 16
#define FORM_INPUT     0
#define FORM_BUTTON    1
#define FORM_TEXTAREA  2
#define FORM_SELECT    3
#define FORM_CHECKBOX  4
#define FORM_PASSWORD  5

#define FORM_VAL_MAX   64
#define FORM_OPT_MAX   8

struct form_field {
    int type;
    int x, y, w, h;            /* position/size in content coords */
    char name[32];
    char value[FORM_VAL_MAX];
    int value_len;
    int cursor;
    bool checked;               /* for checkbox */
    char placeholder[32];
    char options[FORM_OPT_MAX][32]; /* for select */
    int option_count;
    int selected_option;
    bool dropdown_open;
    char onclick[64];
};

/* ===========================================================================
 * Simple JavaScript Engine
 * =========================================================================== */

#define MAX_SCRIPT_FUNCS 8
#define MAX_FUNC_BODY    256
#define MAX_JS_VARS      8

struct js_func {
    char name[32];
    char body[MAX_FUNC_BODY];
    bool used;
};

struct js_var {
    char name[16];
    char value[64];
    bool used;
};

static struct js_func script_funcs[MAX_SCRIPT_FUNCS];
static int script_func_count = 0;
static struct js_var js_vars[MAX_JS_VARS];

static bool alert_active = false;
static char alert_message[128];
static int alert_ok_x, alert_ok_y, alert_ok_w, alert_ok_h;

/* ===========================================================================
 * CSS Engine
 * =========================================================================== */

/* --- Color parsing --- */

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static char to_lower(char c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static bool ci_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (to_lower(*a) != to_lower(*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

static int skip_ws(const char *s, int pos, int len) {
    while (pos < len && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r'))
        pos++;
    return pos;
}

static int parse_int(const char *s, int pos, int len, int *out) {
    int val = 0;
    int start = pos;
    while (pos < len && s[pos] >= '0' && s[pos] <= '9') {
        val = val * 10 + (s[pos] - '0');
        pos++;
    }
    *out = val;
    return pos - start;
}

static color_t parse_color(const char *val) {
    while (*val == ' ') val++;

    if (ci_eq(val, "red"))     return COLOR_RGB(255, 0, 0);
    if (ci_eq(val, "green"))   return COLOR_RGB(0, 128, 0);
    if (ci_eq(val, "blue"))    return COLOR_RGB(0, 0, 255);
    if (ci_eq(val, "white"))   return COLOR_RGB(255, 255, 255);
    if (ci_eq(val, "black"))   return COLOR_RGB(0, 0, 0);
    if (ci_eq(val, "yellow"))  return COLOR_RGB(255, 255, 0);
    if (ci_eq(val, "cyan"))    return COLOR_RGB(0, 255, 255);
    if (ci_eq(val, "orange"))  return COLOR_RGB(255, 165, 0);
    if (ci_eq(val, "purple"))  return COLOR_RGB(128, 0, 128);
    if (ci_eq(val, "grey") || ci_eq(val, "gray")) return COLOR_RGB(128, 128, 128);
    if (ci_eq(val, "pink"))    return COLOR_RGB(255, 192, 203);
    if (ci_eq(val, "brown"))   return COLOR_RGB(139, 69, 19);
    if (ci_eq(val, "navy"))    return COLOR_RGB(0, 0, 128);
    if (ci_eq(val, "teal"))    return COLOR_RGB(0, 128, 128);
    if (ci_eq(val, "lime"))    return COLOR_RGB(0, 255, 0);
    if (ci_eq(val, "maroon"))  return COLOR_RGB(128, 0, 0);
    if (ci_eq(val, "olive"))   return COLOR_RGB(128, 128, 0);
    if (ci_eq(val, "aqua"))    return COLOR_RGB(0, 255, 255);
    if (ci_eq(val, "silver"))  return COLOR_RGB(192, 192, 192);
    if (ci_eq(val, "fuchsia")) return COLOR_RGB(255, 0, 255);
    if (ci_eq(val, "coral"))   return COLOR_RGB(255, 127, 80);
    if (ci_eq(val, "gold"))    return COLOR_RGB(255, 215, 0);
    if (ci_eq(val, "tomato"))  return COLOR_RGB(255, 99, 71);
    if (ci_eq(val, "salmon"))  return COLOR_RGB(250, 128, 114);
    if (ci_eq(val, "khaki"))   return COLOR_RGB(240, 230, 140);
    if (ci_eq(val, "plum"))    return COLOR_RGB(221, 160, 221);
    if (ci_eq(val, "tan"))     return COLOR_RGB(210, 180, 140);
    if (ci_eq(val, "crimson")) return COLOR_RGB(220, 20, 60);
    if (ci_eq(val, "indigo"))  return COLOR_RGB(75, 0, 130);
    if (ci_eq(val, "violet"))  return COLOR_RGB(238, 130, 238);
    if (ci_eq(val, "darkgray") || ci_eq(val, "darkgrey")) return COLOR_RGB(169, 169, 169);
    if (ci_eq(val, "lightgray") || ci_eq(val, "lightgrey")) return COLOR_RGB(211, 211, 211);
    if (ci_eq(val, "darkblue"))  return COLOR_RGB(0, 0, 139);
    if (ci_eq(val, "darkred"))   return COLOR_RGB(139, 0, 0);
    if (ci_eq(val, "darkgreen")) return COLOR_RGB(0, 100, 0);
    if (ci_eq(val, "skyblue"))   return COLOR_RGB(135, 206, 235);
    if (ci_eq(val, "steelblue")) return COLOR_RGB(70, 130, 180);
    if (ci_eq(val, "wheat"))     return COLOR_RGB(245, 222, 179);
    if (ci_eq(val, "linen"))     return COLOR_RGB(250, 240, 230);
    if (ci_eq(val, "ivory"))     return COLOR_RGB(255, 255, 240);
    if (ci_eq(val, "snow"))      return COLOR_RGB(255, 250, 250);

    /* Hex: #RGB or #RRGGBB */
    if (val[0] == '#') {
        val++;
        int len = str_len(val);
        if (len >= 6) {
            int r = hex_digit(val[0]) * 16 + hex_digit(val[1]);
            int g = hex_digit(val[2]) * 16 + hex_digit(val[3]);
            int b = hex_digit(val[4]) * 16 + hex_digit(val[5]);
            if (r >= 0 && g >= 0 && b >= 0) return COLOR_RGB(r, g, b);
        } else if (len >= 3) {
            int r = hex_digit(val[0]); r = r * 16 + r;
            int g = hex_digit(val[1]); g = g * 16 + g;
            int b = hex_digit(val[2]); b = b * 16 + b;
            if (r >= 0 && g >= 0 && b >= 0) return COLOR_RGB(r, g, b);
        }
    }

    /* rgb(r, g, b) */
    if (val[0] == 'r' && val[1] == 'g' && val[2] == 'b' && val[3] == '(') {
        int p = 4;
        int vlen = str_len(val);
        p = skip_ws(val, p, vlen);
        int r = 0, g = 0, b = 0;
        parse_int(val, p, vlen, &r); while (p < vlen && val[p] != ',') p++; p++;
        p = skip_ws(val, p, vlen);
        parse_int(val, p, vlen, &g); while (p < vlen && val[p] != ',') p++; p++;
        p = skip_ws(val, p, vlen);
        parse_int(val, p, vlen, &b);
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        return COLOR_RGB(r, g, b);
    }

    return C_NONE;
}

/* --- CSS Rule Storage --- */

#define MAX_CSS_RULES 32
struct css_rule {
    char selector[32];
    color_t color;
    color_t bg_color;
    bool has_color, has_bg;
    bool bold, italic, underline;
    bool has_bold, has_italic, has_underline;
    int margin_top, margin_bottom, padding_left;
    bool has_margin_top, has_margin_bottom, has_padding_left;
    bool text_center;
    bool text_right;
    bool has_text_align;
    bool display_none;
    bool has_display;
    bool has_border_bottom;
    color_t border_bottom_color;
    /* Extended CSS properties */
    int margin_left, margin_right;
    bool has_margin_left, has_margin_right;
    int padding_top, padding_bottom, padding_right;
    bool has_padding_top, has_padding_bottom, has_padding_right;
    bool has_border;
    int border_width;
    color_t border_color;
    int width, max_width;
    bool has_width, has_max_width;
    int font_size;  /* 0=normal, 1=small, 2=large */
    bool has_font_size;
    int line_height, letter_spacing;
    bool has_line_height, has_letter_spacing;
    int list_style; /* 0=disc, 1=none, 2=circle, 3=square */
    bool has_list_style;
    bool white_space_pre;
    bool has_white_space;
    bool visibility_hidden;
    bool has_visibility;
};

/* --- Render Style Stack --- */

#define STYLE_STACK_MAX 16
struct render_style {
    color_t color;
    color_t bg_color;
    bool bold;
    bool italic;
    bool underline;
    int padding_left;
    bool text_center;
    bool text_right;
    bool display_none;
    bool has_border_bottom;
    color_t border_bottom_color;
    int margin_top;
    int margin_bottom;
    /* Extended properties */
    int margin_left, margin_right;
    int padding_top, padding_bottom, padding_right;
    bool has_border;
    int border_width;
    color_t border_color;
    int width, max_width;
    bool has_width, has_max_width;
    int font_size;  /* 0=normal, 1=small, 2=large */
    int line_height;
    int letter_spacing;
    int list_style; /* 0=disc, 1=none, 2=circle, 3=square */
    bool white_space_pre;
    bool visibility_hidden;
};
static struct render_style style_stack[STYLE_STACK_MAX];
static int style_depth = 0;

static void style_push(void) {
    if (style_depth < STYLE_STACK_MAX - 1) {
        style_stack[style_depth + 1] = style_stack[style_depth];
        style_stack[style_depth + 1].bg_color = C_NONE;
        style_stack[style_depth + 1].has_border_bottom = false;
        style_stack[style_depth + 1].margin_top = 0;
        style_stack[style_depth + 1].margin_bottom = 0;
        style_depth++;
    }
}

static void style_pop(void) {
    if (style_depth > 0) style_depth--;
}

static struct render_style *cur_style(void) {
    return &style_stack[style_depth];
}

/* --- Parse CSS properties from a declaration block --- */

static void apply_css_props(const char *css, int css_len, struct render_style *st) {
    int p = 0;
    while (p < css_len) {
        p = skip_ws(css, p, css_len);
        if (p >= css_len) break;

        char prop[32];
        int pi = 0;
        while (p < css_len && css[p] != ':' && css[p] != ';' && pi < 31) {
            prop[pi++] = to_lower(css[p++]);
        }
        prop[pi] = '\0';
        while (pi > 0 && prop[pi - 1] == ' ') prop[--pi] = '\0';

        if (p >= css_len || css[p] != ':') {
            while (p < css_len && css[p] != ';') p++;
            if (p < css_len) p++;
            continue;
        }
        p++;
        p = skip_ws(css, p, css_len);

        char val[64];
        int vi = 0;
        while (p < css_len && css[p] != ';' && css[p] != '}' && vi < 63) {
            val[vi++] = css[p++];
        }
        val[vi] = '\0';
        while (vi > 0 && val[vi - 1] == ' ') val[--vi] = '\0';
        if (p < css_len && css[p] == ';') p++;

        if (ci_eq(prop, "color")) {
            color_t c = parse_color(val);
            if (c != C_NONE) st->color = c;
        } else if (ci_eq(prop, "background-color") || ci_eq(prop, "background")) {
            color_t c = parse_color(val);
            if (c != C_NONE) st->bg_color = c;
        } else if (ci_eq(prop, "text-decoration")) {
            st->underline = ci_eq(val, "underline");
        } else if (ci_eq(prop, "text-align")) {
            st->text_center = ci_eq(val, "center");
            st->text_right = ci_eq(val, "right");
        } else if (ci_eq(prop, "font-weight")) {
            st->bold = ci_eq(val, "bold") || ci_eq(val, "700") || ci_eq(val, "800") || ci_eq(val, "900");
        } else if (ci_eq(prop, "font-style")) {
            st->italic = ci_eq(val, "italic") || ci_eq(val, "oblique");
        } else if (ci_eq(prop, "margin-top")) {
            int v = 0; parse_int(val, 0, vi, &v);
            st->margin_top = v;
        } else if (ci_eq(prop, "margin-bottom")) {
            int v = 0; parse_int(val, 0, vi, &v);
            st->margin_bottom = v;
        } else if (ci_eq(prop, "padding-left")) {
            int v = 0; parse_int(val, 0, vi, &v);
            st->padding_left = v;
        } else if (ci_eq(prop, "padding-top")) {
            int v = 0; parse_int(val, 0, vi, &v);
            st->padding_top = v;
        } else if (ci_eq(prop, "padding-bottom")) {
            int v = 0; parse_int(val, 0, vi, &v);
            st->padding_bottom = v;
        } else if (ci_eq(prop, "padding-right")) {
            int v = 0; parse_int(val, 0, vi, &v);
            st->padding_right = v;
        } else if (ci_eq(prop, "padding")) {
            /* Shorthand: 1-4 values */
            int vals[4] = {0,0,0,0};
            int vc = 0, vp = 0;
            while (vp < vi && vc < 4) {
                while (vp < vi && val[vp] == ' ') vp++;
                int v = 0; int used = parse_int(val, vp, vi, &v);
                if (used > 0) { vals[vc++] = v; vp += used; }
                while (vp < vi && val[vp] != ' ') vp++;
            }
            if (vc == 1) { st->padding_top = st->padding_bottom = st->padding_left = st->padding_right = vals[0]; }
            else if (vc == 2) { st->padding_top = st->padding_bottom = vals[0]; st->padding_left = st->padding_right = vals[1]; }
            else if (vc == 3) { st->padding_top = vals[0]; st->padding_left = st->padding_right = vals[1]; st->padding_bottom = vals[2]; }
            else if (vc == 4) { st->padding_top = vals[0]; st->padding_right = vals[1]; st->padding_bottom = vals[2]; st->padding_left = vals[3]; }
        } else if (ci_eq(prop, "margin-left")) {
            int v = 0; parse_int(val, 0, vi, &v);
            st->margin_left = v;
        } else if (ci_eq(prop, "margin-right")) {
            int v = 0; parse_int(val, 0, vi, &v);
            st->margin_right = v;
        } else if (ci_eq(prop, "margin")) {
            /* Shorthand: 1-4 values */
            int vals[4] = {0,0,0,0};
            int vc = 0, vp = 0;
            while (vp < vi && vc < 4) {
                while (vp < vi && val[vp] == ' ') vp++;
                int v = 0; int used = parse_int(val, vp, vi, &v);
                if (used > 0) { vals[vc++] = v; vp += used; }
                while (vp < vi && val[vp] != ' ') vp++;
            }
            if (vc == 1) { st->margin_top = st->margin_bottom = st->margin_left = st->margin_right = vals[0]; }
            else if (vc == 2) { st->margin_top = st->margin_bottom = vals[0]; st->margin_left = st->margin_right = vals[1]; }
            else if (vc == 3) { st->margin_top = vals[0]; st->margin_left = st->margin_right = vals[1]; st->margin_bottom = vals[2]; }
            else if (vc == 4) { st->margin_top = vals[0]; st->margin_right = vals[1]; st->margin_bottom = vals[2]; st->margin_left = vals[3]; }
        } else if (ci_eq(prop, "display")) {
            st->display_none = ci_eq(val, "none");
        } else if (ci_eq(prop, "border")) {
            /* Shorthand: Npx solid COLOR */
            int bw = 1;
            parse_int(val, 0, vi, &bw);
            if (bw < 1) bw = 1;
            st->has_border = true;
            st->border_width = bw;
            color_t bc = C_NONE;
            for (int i = 0; i < vi; i++) {
                if (i == 0 || val[i - 1] == ' ') {
                    bc = parse_color(val + i);
                    if (bc != C_NONE) break;
                }
            }
            st->border_color = (bc != C_NONE) ? bc : C_HR;
        } else if (ci_eq(prop, "border-color")) {
            color_t c = parse_color(val);
            if (c != C_NONE) { st->has_border = true; st->border_color = c; }
        } else if (ci_eq(prop, "border-width")) {
            int v = 0; parse_int(val, 0, vi, &v);
            if (v > 0) { st->has_border = true; st->border_width = v; }
        } else if (ci_eq(prop, "border-bottom")) {
            st->has_border_bottom = true;
            char *tok = val;
            color_t bc = C_NONE;
            for (int i = 0; i < vi; i++) {
                if (i == 0 || val[i - 1] == ' ') {
                    bc = parse_color(val + i);
                    if (bc != C_NONE) break;
                }
            }
            st->border_bottom_color = (bc != C_NONE) ? bc : C_HR;
            (void)tok;
        } else if (ci_eq(prop, "width")) {
            int v = 0; parse_int(val, 0, vi, &v);
            if (v > 0) { st->has_width = true; st->width = v; }
        } else if (ci_eq(prop, "max-width")) {
            int v = 0; parse_int(val, 0, vi, &v);
            if (v > 0) { st->has_max_width = true; st->max_width = v; }
        } else if (ci_eq(prop, "font-size")) {
            int v = 0; parse_int(val, 0, vi, &v);
            if (v > 0) {
                if (v < 12) st->font_size = 1;       /* small */
                else if (v > 18) st->font_size = 2;  /* large */
                else st->font_size = 0;               /* normal */
            } else if (ci_eq(val, "small") || ci_eq(val, "x-small") || ci_eq(val, "xx-small")) {
                st->font_size = 1;
            } else if (ci_eq(val, "large") || ci_eq(val, "x-large") || ci_eq(val, "xx-large")) {
                st->font_size = 2;
            }
        } else if (ci_eq(prop, "line-height")) {
            int v = 0; parse_int(val, 0, vi, &v);
            if (v > 0) st->line_height = v;
        } else if (ci_eq(prop, "letter-spacing")) {
            int v = 0; parse_int(val, 0, vi, &v);
            st->letter_spacing = v;
        } else if (ci_eq(prop, "list-style-type") || ci_eq(prop, "list-style")) {
            if (ci_eq(val, "none")) st->list_style = 1;
            else if (ci_eq(val, "circle")) st->list_style = 2;
            else if (ci_eq(val, "square")) st->list_style = 3;
            else st->list_style = 0; /* disc */
        } else if (ci_eq(prop, "white-space")) {
            st->white_space_pre = ci_eq(val, "pre") || ci_eq(val, "pre-wrap");
        } else if (ci_eq(prop, "visibility")) {
            st->visibility_hidden = ci_eq(val, "hidden");
        } else if (ci_eq(prop, "opacity")) {
            int v = 0; parse_int(val, 0, vi, &v);
            if (v == 0 && val[0] == '0') st->visibility_hidden = true;
        }
    }
}

static void apply_rule(struct css_rule *r, struct render_style *st) {
    if (r->has_color) st->color = r->color;
    if (r->has_bg) st->bg_color = r->bg_color;
    if (r->has_bold) st->bold = r->bold;
    if (r->has_italic) st->italic = r->italic;
    if (r->has_underline) st->underline = r->underline;
    if (r->has_margin_top) st->margin_top = r->margin_top;
    if (r->has_margin_bottom) st->margin_bottom = r->margin_bottom;
    if (r->has_margin_left) st->margin_left = r->margin_left;
    if (r->has_margin_right) st->margin_right = r->margin_right;
    if (r->has_padding_left) st->padding_left = r->padding_left;
    if (r->has_padding_top) st->padding_top = r->padding_top;
    if (r->has_padding_bottom) st->padding_bottom = r->padding_bottom;
    if (r->has_padding_right) st->padding_right = r->padding_right;
    if (r->has_text_align) { st->text_center = r->text_center; st->text_right = r->text_right; }
    if (r->has_display) st->display_none = r->display_none;
    if (r->has_border_bottom) {
        st->has_border_bottom = true;
        st->border_bottom_color = r->border_bottom_color;
    }
    if (r->has_border) {
        st->has_border = true;
        st->border_width = r->border_width;
        st->border_color = r->border_color;
    }
    if (r->has_width) { st->has_width = true; st->width = r->width; }
    if (r->has_max_width) { st->has_max_width = true; st->max_width = r->max_width; }
    if (r->has_font_size) st->font_size = r->font_size;
    if (r->has_line_height) st->line_height = r->line_height;
    if (r->has_letter_spacing) st->letter_spacing = r->letter_spacing;
    if (r->has_list_style) st->list_style = r->list_style;
    if (r->has_white_space) st->white_space_pre = r->white_space_pre;
    if (r->has_visibility) st->visibility_hidden = r->visibility_hidden;
}

/* --- Parse <style> block into css_rules --- */

static void parse_css_rule_body(const char *body, int blen, struct css_rule *rule) {
    struct render_style tmp;
    mem_set(&tmp, 0, sizeof(tmp));
    tmp.color = C_NONE;
    tmp.bg_color = C_NONE;
    tmp.border_bottom_color = C_HR;

    apply_css_props(body, blen, &tmp);

    if (tmp.color != C_NONE) { rule->color = tmp.color; rule->has_color = true; }
    if (tmp.bg_color != C_NONE) { rule->bg_color = tmp.bg_color; rule->has_bg = true; }
    if (tmp.bold) { rule->bold = true; rule->has_bold = true; }
    if (tmp.italic) { rule->italic = true; rule->has_italic = true; }
    if (tmp.underline) { rule->underline = true; rule->has_underline = true; }
    if (tmp.margin_top) { rule->margin_top = tmp.margin_top; rule->has_margin_top = true; }
    if (tmp.margin_bottom) { rule->margin_bottom = tmp.margin_bottom; rule->has_margin_bottom = true; }
    if (tmp.padding_left) { rule->padding_left = tmp.padding_left; rule->has_padding_left = true; }
    if (tmp.text_center || tmp.text_right) {
        rule->text_center = tmp.text_center;
        rule->text_right = tmp.text_right;
        rule->has_text_align = true;
    }
    if (tmp.display_none) { rule->display_none = true; rule->has_display = true; }
    if (tmp.has_border_bottom) { rule->has_border_bottom = true; rule->border_bottom_color = tmp.border_bottom_color; }
    /* Extended properties */
    if (tmp.margin_left) { rule->margin_left = tmp.margin_left; rule->has_margin_left = true; }
    if (tmp.margin_right) { rule->margin_right = tmp.margin_right; rule->has_margin_right = true; }
    if (tmp.padding_top) { rule->padding_top = tmp.padding_top; rule->has_padding_top = true; }
    if (tmp.padding_bottom) { rule->padding_bottom = tmp.padding_bottom; rule->has_padding_bottom = true; }
    if (tmp.padding_right) { rule->padding_right = tmp.padding_right; rule->has_padding_right = true; }
    if (tmp.has_border) { rule->has_border = true; rule->border_width = tmp.border_width; rule->border_color = tmp.border_color; }
    if (tmp.has_width) { rule->has_width = true; rule->width = tmp.width; }
    if (tmp.has_max_width) { rule->has_max_width = true; rule->max_width = tmp.max_width; }
    if (tmp.font_size) { rule->font_size = tmp.font_size; rule->has_font_size = true; }
    if (tmp.line_height) { rule->line_height = tmp.line_height; rule->has_line_height = true; }
    if (tmp.letter_spacing) { rule->letter_spacing = tmp.letter_spacing; rule->has_letter_spacing = true; }
    if (tmp.list_style) { rule->list_style = tmp.list_style; rule->has_list_style = true; }
    if (tmp.white_space_pre) { rule->white_space_pre = true; rule->has_white_space = true; }
    if (tmp.visibility_hidden) { rule->visibility_hidden = true; rule->has_visibility = true; }
}

static void parse_style_block(const char *css, int css_len, struct css_rule *rules, int *rule_count) {
    int p = 0;
    while (p < css_len && *rule_count < MAX_CSS_RULES) {
        p = skip_ws(css, p, css_len);
        if (p >= css_len) break;

        if (p + 1 < css_len && css[p] == '/' && css[p + 1] == '*') {
            p += 2;
            while (p + 1 < css_len && !(css[p] == '*' && css[p + 1] == '/')) p++;
            p += 2;
            continue;
        }

        char sel[32];
        int si = 0;
        while (p < css_len && css[p] != '{' && si < 31) {
            sel[si++] = css[p++];
        }
        sel[si] = '\0';
        while (si > 0 && sel[si - 1] == ' ') sel[--si] = '\0';

        if (p >= css_len || css[p] != '{') break;
        p++;

        int body_start = p;
        while (p < css_len && css[p] != '}') p++;
        int body_len = p - body_start;
        if (p < css_len) p++;

        if (si == 0) continue;

        int sel_start = 0;
        for (int i = 0; i <= si; i++) {
            if (i == si || sel[i] == ',') {
                char one_sel[32];
                int os = 0;
                int j = sel_start;
                while (j < i && sel[j] == ' ') j++;
                while (j < i && os < 31) {
                    one_sel[os++] = to_lower(sel[j++]);
                }
                while (os > 0 && one_sel[os - 1] == ' ') os--;
                one_sel[os] = '\0';
                sel_start = i + 1;

                if (os > 0 && *rule_count < MAX_CSS_RULES) {
                    struct css_rule *r = &rules[*rule_count];
                    mem_set(r, 0, sizeof(*r));
                    str_ncopy(r->selector, one_sel, 31);
                    parse_css_rule_body(css + body_start, body_len, r);
                    (*rule_count)++;
                }
            }
        }
    }
}

static void apply_css_for_tag(const char *tag_name, const char *class_name,
                              struct css_rule *rules, int rule_count) {
    struct render_style *st = cur_style();
    char lower_tag[16];
    int ti = 0;
    while (tag_name[ti] && ti < 15) { lower_tag[ti] = to_lower(tag_name[ti]); ti++; }
    lower_tag[ti] = '\0';

    for (int i = 0; i < rule_count; i++) {
        struct css_rule *r = &rules[i];
        bool match = false;

        if (r->selector[0] == '.') {
            if (class_name[0] && ci_eq(r->selector + 1, class_name))
                match = true;
        } else if (ci_eq(r->selector, "*")) {
            match = true;
        } else {
            if (ci_eq(r->selector, lower_tag))
                match = true;
        }

        if (match) apply_rule(r, st);
    }
}

/* --- Extract attribute values from tag_full --- */

static void extract_attr(const char *tag, int tlen, const char *attr_name, char *out, int max) {
    out[0] = '\0';
    int alen = str_len(attr_name);
    for (int i = 0; i < tlen - alen; i++) {
        bool match = true;
        for (int j = 0; j < alen; j++) {
            if (to_lower(tag[i + j]) != to_lower(attr_name[j])) { match = false; break; }
        }
        if (!match) continue;
        int j = i + alen;
        while (j < tlen && tag[j] == ' ') j++;
        if (j >= tlen || tag[j] != '=') continue;
        j++;
        while (j < tlen && tag[j] == ' ') j++;
        char quote = 0;
        if (j < tlen && (tag[j] == '"' || tag[j] == '\'')) { quote = tag[j]; j++; }
        int k = 0;
        while (j < tlen && k < max - 1) {
            if (quote && tag[j] == quote) break;
            if (!quote && (tag[j] == ' ' || tag[j] == '>')) break;
            out[k++] = tag[j++];
        }
        out[k] = '\0';
        return;
    }
}

/* --- HTML Entity Decoding --- */

/* Decode an HTML entity starting at buf[pos] (which should be '&').
 * Returns number of chars consumed (including '&' and ';'), or 0 if invalid.
 * Decoded character written to *out_char. */
static int decode_entity(const char *buf, int pos, int len, char *out_char, bool *out_nbsp) {
    *out_nbsp = false;
    if (pos >= len || buf[pos] != '&') return 0;
    /* Find the ';' */
    int end = pos + 1;
    while (end < len && end < pos + 10 && buf[end] != ';' && buf[end] != ' ' && buf[end] != '<')
        end++;
    if (end >= len || buf[end] != ';') return 0;
    int elen = end - pos - 1; /* length of entity name (between & and ;) */
    const char *e = buf + pos + 1;

    /* Numeric entities: &#NNN; or &#xHH; */
    if (elen >= 2 && e[0] == '#') {
        int val = 0;
        if (e[1] == 'x' || e[1] == 'X') {
            /* Hex */
            for (int i = 2; i < elen; i++) {
                int d = hex_digit(e[i]);
                if (d < 0) return 0;
                val = val * 16 + d;
            }
        } else {
            /* Decimal */
            for (int i = 1; i < elen; i++) {
                if (e[i] < '0' || e[i] > '9') return 0;
                val = val * 10 + (e[i] - '0');
            }
        }
        if (val > 0 && val < 128) { *out_char = (char)val; return end - pos + 1; }
        if (val == 160) { *out_char = ' '; *out_nbsp = true; return end - pos + 1; }
        return 0;
    }

    /* Named entities */
    if (elen == 2 && e[0] == 'l' && e[1] == 't') { *out_char = '<'; return end - pos + 1; }
    if (elen == 2 && e[0] == 'g' && e[1] == 't') { *out_char = '>'; return end - pos + 1; }
    if (elen == 3 && e[0] == 'a' && e[1] == 'm' && e[2] == 'p') { *out_char = '&'; return end - pos + 1; }
    if (elen == 4 && e[0] == 'q' && e[1] == 'u' && e[2] == 'o' && e[3] == 't') { *out_char = '"'; return end - pos + 1; }
    if (elen == 4 && e[0] == 'a' && e[1] == 'p' && e[2] == 'o' && e[3] == 's') { *out_char = '\''; return end - pos + 1; }
    if (elen == 4 && e[0] == 'n' && e[1] == 'b' && e[2] == 's' && e[3] == 'p') { *out_char = ' '; *out_nbsp = true; return end - pos + 1; }

    return 0;
}

/* Case-insensitive tag name compare */
static bool tag_eq(const char *tag, int tlen, const char *name) {
    int nlen = str_len(name);
    if (tlen != nlen) return false;
    for (int i = 0; i < tlen; i++) {
        if (to_lower(tag[i]) != to_lower(name[i])) return false;
    }
    return true;
}

/* ===========================================================================
 * Tab State
 * =========================================================================== */

#define MAX_TABS 4

struct browser_tab {
    char page_buf[PAGE_BUF_SIZE];
    int page_len;
    char addr_buf[ADDR_MAX + 1];
    int addr_len;
    int scroll_y;
    int content_total_h;
    struct link_region links[MAX_LINKS];
    int link_count;
    int hovered_link;
    struct form_field form_fields[MAX_FORM_FIELDS];
    int form_field_count;
    int focused_field;          /* -1 = no form field focused */
    struct css_rule css_rules[MAX_CSS_RULES];
    int css_rule_count;
    char history[HISTORY_MAX][ADDR_MAX + 1];
    int history_count;
    char forward[HISTORY_MAX][ADDR_MAX + 1];
    int forward_count;
    bool used;
};

static struct browser_tab tabs[MAX_TABS];
static int active_tab = 0;
static int tab_count = 0;
static bool addr_focused = false;
static int win_id = -1;
static char status_msg[64];

#define T (&tabs[active_tab])

/* Extract and parse <style> blocks from a tab's page_buf */
static void extract_and_parse_styles(struct browser_tab *tab) {
    tab->css_rule_count = 0;
    int p = 0;
    while (p < tab->page_len - 7) {
        if (tab->page_buf[p] == '<' &&
            to_lower(tab->page_buf[p+1]) == 's' && to_lower(tab->page_buf[p+2]) == 't' &&
            to_lower(tab->page_buf[p+3]) == 'y' && to_lower(tab->page_buf[p+4]) == 'l' &&
            to_lower(tab->page_buf[p+5]) == 'e') {
            p += 6;
            while (p < tab->page_len && tab->page_buf[p] != '>') p++;
            if (p < tab->page_len) p++;
            int start = p;
            while (p < tab->page_len - 8) {
                if (tab->page_buf[p] == '<' && tab->page_buf[p+1] == '/' &&
                    to_lower(tab->page_buf[p+2]) == 's' && to_lower(tab->page_buf[p+3]) == 't' &&
                    to_lower(tab->page_buf[p+4]) == 'y' && to_lower(tab->page_buf[p+5]) == 'l' &&
                    to_lower(tab->page_buf[p+6]) == 'e') {
                    parse_style_block(tab->page_buf + start, p - start,
                                      tab->css_rules, &tab->css_rule_count);
                    break;
                }
                p++;
            }
        }
        p++;
    }
}

/* Extract and parse <script> blocks for function definitions */
static void parse_script_blocks(struct browser_tab *tab) {
    script_func_count = 0;
    mem_set(js_vars, 0, sizeof(js_vars));
    int p = 0;
    while (p < tab->page_len - 8) {
        if (tab->page_buf[p] == '<' &&
            to_lower(tab->page_buf[p+1]) == 's' && to_lower(tab->page_buf[p+2]) == 'c' &&
            to_lower(tab->page_buf[p+3]) == 'r' && to_lower(tab->page_buf[p+4]) == 'i' &&
            to_lower(tab->page_buf[p+5]) == 'p' && to_lower(tab->page_buf[p+6]) == 't') {
            p += 7;
            while (p < tab->page_len && tab->page_buf[p] != '>') p++;
            if (p < tab->page_len) p++;
            int start = p;
            int end = p;
            while (end < tab->page_len - 9) {
                if (tab->page_buf[end] == '<' && tab->page_buf[end+1] == '/' &&
                    to_lower(tab->page_buf[end+2]) == 's' && to_lower(tab->page_buf[end+3]) == 'c' &&
                    to_lower(tab->page_buf[end+4]) == 'r' && to_lower(tab->page_buf[end+5]) == 'i' &&
                    to_lower(tab->page_buf[end+6]) == 'p' && to_lower(tab->page_buf[end+7]) == 't') {
                    break;
                }
                end++;
            }
            /* Parse function definitions: function name() { body } */
            int sp = start;
            while (sp < end && script_func_count < MAX_SCRIPT_FUNCS) {
                sp = skip_ws(tab->page_buf, sp, end);
                /* Look for "function " */
                if (sp + 9 < end &&
                    tab->page_buf[sp] == 'f' && tab->page_buf[sp+1] == 'u' &&
                    tab->page_buf[sp+2] == 'n' && tab->page_buf[sp+3] == 'c' &&
                    tab->page_buf[sp+4] == 't' && tab->page_buf[sp+5] == 'i' &&
                    tab->page_buf[sp+6] == 'o' && tab->page_buf[sp+7] == 'n' &&
                    tab->page_buf[sp+8] == ' ') {
                    sp += 9;
                    sp = skip_ws(tab->page_buf, sp, end);
                    /* Extract function name */
                    struct js_func *fn = &script_funcs[script_func_count];
                    mem_set(fn, 0, sizeof(*fn));
                    int ni = 0;
                    while (sp < end && tab->page_buf[sp] != '(' && ni < 31) {
                        fn->name[ni++] = tab->page_buf[sp++];
                    }
                    fn->name[ni] = '\0';
                    /* Trim trailing spaces */
                    while (ni > 0 && fn->name[ni-1] == ' ') fn->name[--ni] = '\0';
                    /* Skip past () */
                    while (sp < end && tab->page_buf[sp] != '{') sp++;
                    if (sp < end) sp++; /* skip '{' */
                    /* Collect body until '}' */
                    int bi = 0;
                    while (sp < end && tab->page_buf[sp] != '}' && bi < MAX_FUNC_BODY - 1) {
                        fn->body[bi++] = tab->page_buf[sp++];
                    }
                    fn->body[bi] = '\0';
                    if (sp < end) sp++; /* skip '}' */
                    fn->used = true;
                    script_func_count++;
                } else {
                    sp++;
                }
            }
            p = end;
        }
        p++;
    }
}

/* Forward declarations for JS engine */
static void load_page(struct browser_tab *tab, const char *filename);

/* --- JS helpers --- */

static void js_extract_string_arg(const char *stmt, int start, char *out, int max) {
    out[0] = '\0';
    int p = start;
    int slen = str_len(stmt);
    /* Find opening quote */
    while (p < slen && stmt[p] != '\'' && stmt[p] != '"') p++;
    if (p >= slen) return;
    char quote = stmt[p++];
    int oi = 0;
    while (p < slen && stmt[p] != quote && oi < max - 1)
        out[oi++] = stmt[p++];
    out[oi] = '\0';
}

static void js_set_var(const char *name, const char *value) {
    /* Update existing */
    for (int i = 0; i < MAX_JS_VARS; i++) {
        if (js_vars[i].used && str_eq(js_vars[i].name, name)) {
            str_ncopy(js_vars[i].value, value, 63);
            return;
        }
    }
    /* Add new */
    for (int i = 0; i < MAX_JS_VARS; i++) {
        if (!js_vars[i].used) {
            js_vars[i].used = true;
            str_ncopy(js_vars[i].name, name, 15);
            str_ncopy(js_vars[i].value, value, 63);
            return;
        }
    }
}

static void js_exec(struct browser_tab *tab, const char *code) {
    char stmt[128];
    int clen = str_len(code);
    int p = 0;

    while (p < clen) {
        /* Skip whitespace */
        while (p < clen && (code[p] == ' ' || code[p] == '\n' || code[p] == '\r' || code[p] == '\t'))
            p++;
        if (p >= clen) break;

        /* Extract statement until ';' or end */
        int si = 0;
        while (p < clen && code[p] != ';' && si < 127)
            stmt[si++] = code[p++];
        stmt[si] = '\0';
        if (p < clen && code[p] == ';') p++;

        /* Trim trailing whitespace */
        while (si > 0 && (stmt[si-1] == ' ' || stmt[si-1] == '\n' || stmt[si-1] == '\r'))
            stmt[--si] = '\0';
        if (si == 0) continue;

        /* Trim leading whitespace */
        int ss = 0;
        while (stmt[ss] == ' ' || stmt[ss] == '\t') ss++;

        /* Dispatch: alert('msg') */
        if (str_starts_with(stmt + ss, "alert(")) {
            js_extract_string_arg(stmt, ss + 6, alert_message, 128);
            alert_active = true;
            continue;
        }

        /* navigate('file') */
        if (str_starts_with(stmt + ss, "navigate(")) {
            char target[ADDR_MAX + 1];
            js_extract_string_arg(stmt, ss + 9, target, ADDR_MAX);
            if (target[0]) load_page(tab, target);
            return; /* Navigation replaces page — stop executing */
        }

        /* document.title = 'text' */
        if (str_starts_with(stmt + ss, "document.title")) {
            int eq = ss + 14;
            while (stmt[eq] == ' ') eq++;
            if (stmt[eq] == '=') {
                eq++;
                char title[64];
                js_extract_string_arg(stmt, eq, title, 64);
                if (title[0]) {
                    struct window *w = wm_get_window(win_id);
                    if (w) str_ncopy(w->title, title, 63);
                }
            }
            continue;
        }

        /* functionName() — call a script function */
        int paren = ss;
        while (paren < si && stmt[paren] != '(') paren++;
        if (paren < si && stmt[paren] == '(' && stmt[paren + 1] == ')') {
            char fname[32];
            int fi = 0;
            for (int j = ss; j < paren && fi < 31; j++)
                fname[fi++] = stmt[j];
            fname[fi] = '\0';
            /* Trim */
            while (fi > 0 && fname[fi-1] == ' ') fname[--fi] = '\0';
            /* Look up function */
            for (int j = 0; j < script_func_count; j++) {
                if (script_funcs[j].used && str_eq(script_funcs[j].name, fname)) {
                    js_exec(tab, script_funcs[j].body);
                    break;
                }
            }
            continue;
        }

        /* var = value (simple assignment) */
        int eqi = ss;
        while (eqi < si && stmt[eqi] != '=') eqi++;
        if (eqi < si && stmt[eqi] == '=') {
            char vname[16];
            int vni = 0;
            for (int j = ss; j < eqi && vni < 15; j++) {
                if (stmt[j] != ' ') vname[vni++] = stmt[j];
            }
            vname[vni] = '\0';
            int vstart = eqi + 1;
            while (stmt[vstart] == ' ') vstart++;
            /* Try to extract quoted string, otherwise use raw */
            char val[64];
            if (stmt[vstart] == '\'' || stmt[vstart] == '"') {
                js_extract_string_arg(stmt, vstart, val, 64);
            } else {
                int vi = 0;
                for (int j = vstart; j < si && vi < 63; j++)
                    val[vi++] = stmt[j];
                val[vi] = '\0';
                while (vi > 0 && val[vi-1] == ' ') val[--vi] = '\0';
            }
            js_set_var(vname, val);
        }
    }
}

/* ---- Drawing helpers ---- */

static void brw_rect(color_t *buf, int cw, int ch, int x, int y, int w, int h, color_t c) {
    for (int py = y; py < y + h && py < ch; py++) {
        if (py < 0) continue;
        for (int px = x; px < x + w && px < cw; px++) {
            if (px >= 0) buf[py * cw + px] = c;
        }
    }
}

static void brw_text(color_t *buf, int cw, int ch, int px, int py,
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

static void brw_char_transparent(color_t *buf, int cw, int ch, int px, int py,
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

static void brw_underline(color_t *buf, int cw, int ch, int px, int py, int char_w, color_t c) {
    int uy = py + 15;
    if (uy >= 0 && uy < ch) {
        for (int x = px; x < px + char_w && x < cw; x++) {
            if (x >= 0) buf[uy * cw + x] = c;
        }
    }
}

/* ---- Page loading ---- */

static void load_page(struct browser_tab *tab, const char *filename) {
    if (tab->addr_len > 0 && tab->history_count < HISTORY_MAX) {
        str_copy(tab->history[tab->history_count], tab->addr_buf);
        tab->history_count++;
    }
    tab->forward_count = 0; /* New navigation clears forward history */

    int i = 0;
    while (filename[i] && i < ADDR_MAX) { tab->addr_buf[i] = filename[i]; i++; }
    tab->addr_buf[i] = '\0';
    tab->addr_len = i;

    int idx = fs_find(filename);
    if (idx < 0) {
        const char *err = "<h1>File Not Found</h1><p>Could not find: ";
        int elen = str_len(err);
        mem_copy(tab->page_buf, err, (uint32_t)elen);
        int pos = elen;
        for (int j = 0; filename[j] && pos < PAGE_BUF_SIZE - 10; j++)
            tab->page_buf[pos++] = filename[j];
        tab->page_buf[pos++] = '<'; tab->page_buf[pos++] = '/';
        tab->page_buf[pos++] = 'p'; tab->page_buf[pos++] = '>';
        tab->page_len = pos;
    } else {
        struct fs_node *f = fs_get_node(idx);
        if (!f || f->type != FS_FILE || !f->data) {
            const char *err = "<h1>Error</h1><p>Cannot read file.</p>";
            tab->page_len = str_len(err);
            mem_copy(tab->page_buf, err, (uint32_t)tab->page_len);
        } else {
            int len = (int)f->size;
            if (len > PAGE_BUF_SIZE - 1) len = PAGE_BUF_SIZE - 1;
            mem_copy(tab->page_buf, f->data, (uint32_t)len);
            tab->page_len = len;
        }
    }
    tab->page_buf[tab->page_len] = '\0';

    extract_and_parse_styles(tab);
    parse_script_blocks(tab);
    tab->scroll_y = 0;
    tab->link_count = 0;
    tab->hovered_link = -1;
    tab->form_field_count = 0;
    tab->focused_field = -1;
    str_copy(status_msg, "Ready");
}

static void navigate_back(struct browser_tab *tab) {
    if (tab->history_count <= 0) return;
    /* Save current page to forward history */
    if (tab->addr_len > 0 && tab->forward_count < HISTORY_MAX) {
        str_copy(tab->forward[tab->forward_count], tab->addr_buf);
        tab->forward_count++;
    }
    tab->history_count--;
    int i = 0;
    const char *filename = tab->history[tab->history_count];
    while (filename[i] && i < ADDR_MAX) { tab->addr_buf[i] = filename[i]; i++; }
    tab->addr_buf[i] = '\0';
    tab->addr_len = i;

    int idx = fs_find(filename);
    if (idx >= 0) {
        struct fs_node *f = fs_get_node(idx);
        if (f && f->type == FS_FILE && f->data) {
            int len = (int)f->size;
            if (len > PAGE_BUF_SIZE - 1) len = PAGE_BUF_SIZE - 1;
            mem_copy(tab->page_buf, f->data, (uint32_t)len);
            tab->page_len = len;
            tab->page_buf[tab->page_len] = '\0';
        }
    }
    extract_and_parse_styles(tab);
    parse_script_blocks(tab);
    tab->scroll_y = 0;
    tab->link_count = 0;
    tab->hovered_link = -1;
    tab->form_field_count = 0;
    tab->focused_field = -1;
    str_copy(status_msg, "Ready");
}

static void navigate_forward(struct browser_tab *tab) {
    if (tab->forward_count <= 0) return;
    /* Save current page to back history */
    if (tab->addr_len > 0 && tab->history_count < HISTORY_MAX) {
        str_copy(tab->history[tab->history_count], tab->addr_buf);
        tab->history_count++;
    }
    tab->forward_count--;
    int i = 0;
    const char *filename = tab->forward[tab->forward_count];
    while (filename[i] && i < ADDR_MAX) { tab->addr_buf[i] = filename[i]; i++; }
    tab->addr_buf[i] = '\0';
    tab->addr_len = i;

    int idx = fs_find(filename);
    if (idx >= 0) {
        struct fs_node *f = fs_get_node(idx);
        if (f && f->type == FS_FILE && f->data) {
            int len = (int)f->size;
            if (len > PAGE_BUF_SIZE - 1) len = PAGE_BUF_SIZE - 1;
            mem_copy(tab->page_buf, f->data, (uint32_t)len);
            tab->page_len = len;
            tab->page_buf[tab->page_len] = '\0';
        }
    }
    extract_and_parse_styles(tab);
    parse_script_blocks(tab);
    tab->scroll_y = 0;
    tab->link_count = 0;
    tab->hovered_link = -1;
    tab->form_field_count = 0;
    tab->focused_field = -1;
    str_copy(status_msg, "Ready");
}

/* ---- Tab management ---- */

static int new_tab(const char *url) {
    if (tab_count >= MAX_TABS) return -1;
    int idx = -1;
    for (int i = 0; i < MAX_TABS; i++) {
        if (!tabs[i].used) { idx = i; break; }
    }
    if (idx < 0) return -1;
    mem_set(&tabs[idx], 0, sizeof(struct browser_tab));
    tabs[idx].used = true;
    tabs[idx].hovered_link = -1;
    tab_count++;
    active_tab = idx;
    load_page(&tabs[idx], url);
    return idx;
}

static void close_tab(int idx) {
    if (tab_count <= 1) return;
    if (idx < 0 || idx >= MAX_TABS || !tabs[idx].used) return;
    tabs[idx].used = false;
    tab_count--;
    if (active_tab == idx) {
        /* Find next available tab */
        for (int i = 0; i < MAX_TABS; i++) {
            if (tabs[i].used) { active_tab = i; break; }
        }
    }
}

/* ---- Event handler ---- */

static void brw_on_event(struct window *win, struct gui_event *evt) {
    /* Alert modal intercepts all input */
    if (alert_active) {
        if (evt->type == EVT_KEY_PRESS) {
            uint8_t k = evt->key;
            if (k == '\n' || k == ' ' || k == 0x1B) alert_active = false;
        }
        if (evt->type == EVT_MOUSE_DOWN) {
            int mx = evt->mouse_x - (win->x + BORDER_WIDTH);
            int my = evt->mouse_y - (win->y + TITLEBAR_HEIGHT);
            if (mx >= alert_ok_x && mx < alert_ok_x + alert_ok_w &&
                my >= alert_ok_y && my < alert_ok_y + alert_ok_h)
                alert_active = false;
        }
        return;
    }

    if (evt->type == EVT_KEY_PRESS) {
        uint8_t k = evt->key;

        /* Ctrl+T = new tab (ASCII 0x14) */
        if (k == 0x14) { new_tab("home.htm"); return; }
        /* Ctrl+W = close tab (ASCII 0x17) */
        if (k == 0x17) { close_tab(active_tab); return; }

        if (addr_focused) {
            if (k == '\n') {
                if (T->addr_len > 0) {
                    T->addr_buf[T->addr_len] = '\0';
                    char tmp[ADDR_MAX + 1];
                    str_copy(tmp, T->addr_buf);
                    T->addr_len = 0;
                    load_page(T, tmp);
                }
                addr_focused = false;
                return;
            }
            if (k == 0x1B) { addr_focused = false; return; }
            if (k == '\b') { if (T->addr_len > 0) T->addr_len--; return; }
            if (k >= 0x20 && k < 0x80 && T->addr_len < ADDR_MAX) {
                T->addr_buf[T->addr_len++] = (char)k;
                return;
            }
            return;
        }

        /* Form field keyboard input */
        if (T->focused_field >= 0 && T->focused_field < T->form_field_count) {
            struct form_field *ff = &T->form_fields[T->focused_field];
            if (k == 0x1B) { T->focused_field = -1; return; } /* Escape unfocuses */
            if (k == '\t') {
                /* Tab to next form field, or wrap to address bar */
                int next = T->focused_field + 1;
                if (next >= T->form_field_count) {
                    T->focused_field = -1;
                    addr_focused = true;
                } else {
                    T->focused_field = next;
                }
                return;
            }
            if (ff->type == FORM_BUTTON) {
                if (k == ' ' || k == '\n') {
                    if (ff->onclick[0]) js_exec(T, ff->onclick);
                    return;
                }
            } else if (ff->type == FORM_CHECKBOX) {
                if (k == ' ' || k == '\n') { ff->checked = !ff->checked; return; }
            } else if (ff->type == FORM_SELECT) {
                if (k == KEY_UP && ff->selected_option > 0) { ff->selected_option--; return; }
                if (k == KEY_DOWN && ff->selected_option < ff->option_count - 1) { ff->selected_option++; return; }
                if (k == ' ' || k == '\n') { ff->dropdown_open = !ff->dropdown_open; return; }
            } else if (ff->type == FORM_INPUT || ff->type == FORM_PASSWORD || ff->type == FORM_TEXTAREA) {
                if (k == '\b') {
                    if (ff->cursor > 0) {
                        for (int i = ff->cursor - 1; i < ff->value_len - 1; i++)
                            ff->value[i] = ff->value[i + 1];
                        ff->value_len--;
                        ff->cursor--;
                    }
                    return;
                }
                if (k == KEY_LEFT && ff->cursor > 0) { ff->cursor--; return; }
                if (k == KEY_RIGHT && ff->cursor < ff->value_len) { ff->cursor++; return; }
                if (k == KEY_HOME) { ff->cursor = 0; return; }
                if (k == KEY_END) { ff->cursor = ff->value_len; return; }
                if (k == KEY_DELETE) {
                    if (ff->cursor < ff->value_len) {
                        for (int i = ff->cursor; i < ff->value_len - 1; i++)
                            ff->value[i] = ff->value[i + 1];
                        ff->value_len--;
                    }
                    return;
                }
                if (k == '\n' && ff->type == FORM_TEXTAREA) {
                    /* Newline in textarea — don't handle for now, just unfocus */
                    T->focused_field = -1;
                    return;
                }
                if (k == '\n') { T->focused_field = -1; return; } /* Enter = unfocus input */
                if (k >= 0x20 && k < 0x80 && ff->value_len < FORM_VAL_MAX - 1) {
                    /* Insert char at cursor */
                    for (int i = ff->value_len; i > ff->cursor; i--)
                        ff->value[i] = ff->value[i - 1];
                    ff->value[ff->cursor] = (char)k;
                    ff->value_len++;
                    ff->cursor++;
                    return;
                }
            }
            return;
        }

        if (k == KEY_UP)   { T->scroll_y -= 16; if (T->scroll_y < 0) T->scroll_y = 0; return; }
        if (k == KEY_DOWN) { T->scroll_y += 16; return; }
        if (k == KEY_PGUP) { T->scroll_y -= CONTENT_H; if (T->scroll_y < 0) T->scroll_y = 0; return; }
        if (k == KEY_PGDN) { T->scroll_y += CONTENT_H; return; }
        if (k == KEY_HOME) { T->scroll_y = 0; return; }
        if (k == KEY_END)  { T->scroll_y = T->content_total_h - CONTENT_H; if (T->scroll_y < 0) T->scroll_y = 0; return; }
        if (k == '\b') { navigate_back(T); return; }
        if (k == '\t') {
            /* Tab cycles: scrolling -> form fields -> address bar */
            if (T->form_field_count > 0) {
                T->focused_field = 0;
            } else {
                addr_focused = true;
            }
            return;
        }
        if (k == 12) { addr_focused = true; T->addr_len = 0; return; }
        return;
    }

    if (evt->type == EVT_MOUSE_DOWN) {
        int mx = evt->mouse_x - (win->x + BORDER_WIDTH);
        int my = evt->mouse_y - (win->y + TITLEBAR_HEIGHT);

        /* Toolbar: back button */
        if (mx >= 4 && mx < 34 && my >= 4 && my < 22) { navigate_back(T); return; }
        /* Toolbar: forward button */
        if (mx >= 38 && mx < 68 && my >= 4 && my < 22) { navigate_forward(T); return; }
        /* Toolbar: Go button */
        if (mx >= BRW_W - 36 && mx < BRW_W - 4 && my >= 4 && my < 22) {
            if (T->addr_len > 0) {
                T->addr_buf[T->addr_len] = '\0';
                char tmp[ADDR_MAX + 1];
                str_copy(tmp, T->addr_buf);
                T->addr_len = 0;
                load_page(T, tmp);
            }
            return;
        }
        /* Toolbar: address bar */
        if (mx >= ADDR_X && mx < ADDR_X + ADDR_W && my >= 4 && my < 22) { addr_focused = true; return; }

        /* Tab bar clicks */
        if (my >= TOOLBAR_H && my < TOOLBAR_H + TAB_BAR_H) {
            int tx = 2;
            for (int i = 0; i < MAX_TABS; i++) {
                if (!tabs[i].used) continue;
                int tw = str_len(tabs[i].addr_buf) * 8 + 28; /* text + padding + close btn */
                if (tw < 60) tw = 60;
                if (tw > 120) tw = 120;
                if (mx >= tx && mx < tx + tw) {
                    /* Check if click is on close "x" (rightmost 16px of tab) */
                    if (mx >= tx + tw - 16 && tab_count > 1) {
                        close_tab(i);
                    } else {
                        active_tab = i;
                    }
                    return;
                }
                tx += tw + 2;
            }
            /* "+" new tab button */
            if (mx >= tx && mx < tx + 20 && tab_count < MAX_TABS) {
                new_tab("home.htm");
                return;
            }
            return;
        }

        /* Content area: link clicks and form field clicks */
        if (my >= CONTENT_Y && my < CONTENT_Y + CONTENT_H) {
            int content_mx = mx;
            int content_my = my - CONTENT_Y + T->scroll_y;

            /* Check form fields first */
            T->focused_field = -1;
            for (int i = 0; i < T->form_field_count; i++) {
                struct form_field *ff = &T->form_fields[i];
                if (content_mx >= ff->x && content_mx < ff->x + ff->w &&
                    content_my >= ff->y && content_my < ff->y + ff->h) {
                    T->focused_field = i;
                    addr_focused = false;
                    if (ff->type == FORM_BUTTON) {
                        if (ff->onclick[0]) { js_exec(T, ff->onclick); return; }
                    } else if (ff->type == FORM_CHECKBOX) {
                        ff->checked = !ff->checked;
                    } else if (ff->type == FORM_SELECT) {
                        if (ff->dropdown_open) {
                            /* Click on dropdown option */
                            int opt_y = ff->y + ff->h;
                            int opt_idx = (content_my - opt_y) / 16;
                            if (opt_idx >= 0 && opt_idx < ff->option_count) {
                                ff->selected_option = opt_idx;
                            }
                            ff->dropdown_open = false;
                        } else {
                            ff->dropdown_open = true;
                        }
                    } else if (ff->type == FORM_INPUT || ff->type == FORM_PASSWORD) {
                        /* Place cursor based on click position */
                        int rel_x = content_mx - ff->x - 4;
                        ff->cursor = rel_x / 8;
                        if (ff->cursor < 0) ff->cursor = 0;
                        if (ff->cursor > ff->value_len) ff->cursor = ff->value_len;
                    }
                    return;
                }
            }

            /* Check links */
            for (int i = 0; i < T->link_count; i++) {
                if (content_mx >= T->links[i].x && content_mx < T->links[i].x + T->links[i].w &&
                    content_my >= T->links[i].y && content_my < T->links[i].y + T->links[i].h) {
                    load_page(T, T->links[i].href); return;
                }
            }
            addr_focused = false;
        }
    }

    if (evt->type == EVT_MOUSE_MOVE) {
        int mx = evt->mouse_x - (win->x + BORDER_WIDTH);
        int my = evt->mouse_y - (win->y + TITLEBAR_HEIGHT);
        T->hovered_link = -1;
        if (my >= CONTENT_Y && my < CONTENT_Y + CONTENT_H) {
            int content_mx = mx;
            int content_my = my - CONTENT_Y + T->scroll_y;
            for (int i = 0; i < T->link_count; i++) {
                if (content_mx >= T->links[i].x && content_mx < T->links[i].x + T->links[i].w &&
                    content_my >= T->links[i].y && content_my < T->links[i].y + T->links[i].h) {
                    T->hovered_link = i;
                    str_copy(status_msg, T->links[i].href);
                    return;
                }
            }
        }
        str_copy(status_msg, "Ready");
    }
}

/* ---- Rendering ---- */

void browser_create(void) {
    if (win_id >= 0) {
        struct window *w = wm_get_window(win_id);
        if (w && w->alive && w->on_event == brw_on_event) { wm_focus_window(win_id); return; }
    }
    win_id = wm_create_window("Browser", 40, 30, BRW_W, BRW_H, brw_on_event, NULL);
    str_copy(status_msg, "Ready");
    tab_count = 0;
    active_tab = 0;
    for (int i = 0; i < MAX_TABS; i++) tabs[i].used = false;
    new_tab("home.htm");
}

bool browser_is_alive(void) {
    if (win_id < 0) return false;
    struct window *w = wm_get_window(win_id);
    return (w && w->alive);
}

/* Measure next word width in pixels */
static int measure_word(const char *pbuf, int plen, int pos) {
    int chars = 0;
    while (pos < plen) {
        char c = pbuf[pos];
        if (c == '<' || c == ' ' || c == '\t' || c == '\n' || c == '\r') break;
        chars++;
        pos++;
    }
    return chars * 8;
}

/* Measure line width for text alignment */
static int measure_line_width(const char *pbuf, int plen, int pos, int start_x, int max_w) {
    int x = start_x;
    while (pos < plen) {
        char c = pbuf[pos];
        if (c == '<') {
            int tp = pos + 1;
            if (tp < plen && pbuf[tp] == '/') tp++;
            char tn[16];
            int ti = 0;
            while (tp < plen && pbuf[tp] != '>' && pbuf[tp] != ' ' && ti < 15)
                tn[ti++] = to_lower(pbuf[tp++]);
            tn[ti] = '\0';
            if (ti > 0 && (
                tag_eq(tn, ti, "p") || tag_eq(tn, ti, "div") || tag_eq(tn, ti, "br") ||
                tag_eq(tn, ti, "h1") || tag_eq(tn, ti, "h2") || tag_eq(tn, ti, "h3") ||
                tag_eq(tn, ti, "h4") || tag_eq(tn, ti, "h5") || tag_eq(tn, ti, "h6") ||
                tag_eq(tn, ti, "li") || tag_eq(tn, ti, "hr") || tag_eq(tn, ti, "ul") ||
                tag_eq(tn, ti, "ol") || tag_eq(tn, ti, "tr") ||
                tag_eq(tn, ti, "blockquote")))
                break;
            while (pos < plen && pbuf[pos] != '>') pos++;
            if (pos < plen) pos++;
            continue;
        }
        if (c == '\n' || c == '\r') { pos++; continue; }
        if (c == ' ') {
            int ww = measure_word(pbuf, plen, pos + 1);
            if (ww > 0 && x + 8 + ww > max_w) break;
        }
        if (x + 8 > max_w) break;
        x += 8;
        pos++;
    }
    return x - start_x;
}

void browser_render(void) {
    if (win_id < 0) return;
    struct window *win = wm_get_window(win_id);
    if (!win || !win->alive || !win->content || win->on_event != brw_on_event) { win_id = -1; return; }
    if (!T->used) return;

    int cw = win->content_w;
    int ch = win->content_h;
    color_t *buf = win->content;

    for (int i = 0; i < cw * ch; i++) buf[i] = C_BG;

    /* Toolbar */
    brw_rect(buf, cw, ch, 0, 0, cw, TOOLBAR_H, C_TOOLBAR);
    /* Back button */
    brw_rect(buf, cw, ch, 4, 4, 30, 18, C_BTN_BG);
    brw_text(buf, cw, ch, 8, 5, "<-", C_BTN_FG, C_BTN_BG);
    /* Forward button */
    brw_rect(buf, cw, ch, 38, 4, 30, 18, C_BTN_BG);
    brw_text(buf, cw, ch, 42, 5, "->", C_BTN_FG, C_BTN_BG);
    brw_rect(buf, cw, ch, ADDR_X, 4, ADDR_W, 18, C_ADDR_BG);
    brw_rect(buf, cw, ch, ADDR_X, 4, ADDR_W, 1, COLOR_RGB(180, 180, 180));
    brw_rect(buf, cw, ch, ADDR_X, 21, ADDR_W, 1, COLOR_RGB(180, 180, 180));
    brw_rect(buf, cw, ch, ADDR_X, 4, 1, 18, COLOR_RGB(180, 180, 180));
    brw_rect(buf, cw, ch, ADDR_X + ADDR_W - 1, 4, 1, 18, COLOR_RGB(180, 180, 180));
    T->addr_buf[T->addr_len] = '\0';
    if (T->addr_len > 0) {
        int max_chars = (ADDR_W - 8) / 8;
        int start = 0;
        if (T->addr_len > max_chars) start = T->addr_len - max_chars;
        brw_text(buf, cw, ch, ADDR_X + 4, 5, T->addr_buf + start, C_ADDR_FG, C_ADDR_BG);
    }
    if (addr_focused && (timer_get_ticks() / 40) & 1) {
        int max_chars = (ADDR_W - 8) / 8;
        int visible_len = T->addr_len;
        if (visible_len > max_chars) visible_len = max_chars;
        brw_rect(buf, cw, ch, ADDR_X + 4 + visible_len * 8, 5, 2, 16, COLOR_RGB(0, 80, 200));
    }
    brw_rect(buf, cw, ch, BRW_W - 36, 4, 32, 18, C_BTN_BG);
    brw_text(buf, cw, ch, BRW_W - 32, 5, "Go", C_BTN_FG, C_BTN_BG);

    /* Tab bar */
    brw_rect(buf, cw, ch, 0, TOOLBAR_H, cw, TAB_BAR_H, C_TAB_BG);
    int tx = 2;
    for (int i = 0; i < MAX_TABS; i++) {
        if (!tabs[i].used) continue;
        int name_len = tabs[i].addr_len;
        int tw = name_len * 8 + 28;
        if (tw < 60) tw = 60;
        if (tw > 120) tw = 120;
        color_t tbg = (i == active_tab) ? C_TAB_ACT : C_TAB_BG;
        brw_rect(buf, cw, ch, tx, TOOLBAR_H + 1, tw, TAB_BAR_H - 1, tbg);
        /* Tab border */
        brw_rect(buf, cw, ch, tx, TOOLBAR_H + 1, 1, TAB_BAR_H - 1, COLOR_RGB(180, 180, 185));
        brw_rect(buf, cw, ch, tx + tw - 1, TOOLBAR_H + 1, 1, TAB_BAR_H - 1, COLOR_RGB(180, 180, 185));
        if (i == active_tab)
            brw_rect(buf, cw, ch, tx, TOOLBAR_H, tw, 1, C_TAB_ACT);
        else
            brw_rect(buf, cw, ch, tx, TOOLBAR_H, tw, 1, COLOR_RGB(180, 180, 185));
        /* Tab title (truncated) */
        tabs[i].addr_buf[tabs[i].addr_len] = '\0';
        int max_title = (tw - 24) / 8;
        if (max_title > 0) {
            char title[16];
            int tl = 0;
            while (tl < max_title && tl < 15 && tabs[i].addr_buf[tl]) {
                title[tl] = tabs[i].addr_buf[tl]; tl++;
            }
            title[tl] = '\0';
            brw_text(buf, cw, ch, tx + 4, TOOLBAR_H + 2, title, C_TAB_FG, tbg);
        }
        /* Close "x" */
        brw_text(buf, cw, ch, tx + tw - 14, TOOLBAR_H + 2, "x", C_TAB_CLOSE, tbg);
        tx += tw + 2;
    }
    /* "+" new tab button */
    if (tab_count < MAX_TABS) {
        brw_rect(buf, cw, ch, tx, TOOLBAR_H + 1, 20, TAB_BAR_H - 1, C_TAB_BG);
        brw_text(buf, cw, ch, tx + 6, TOOLBAR_H + 2, "+", C_TAB_FG, C_TAB_BG);
    }

    /* ---- Render HTML content with CSS ---- */
    T->link_count = 0;
    /* Save form field user data before re-rendering */
    static struct form_field saved_fields[MAX_FORM_FIELDS];
    int saved_field_count = T->form_field_count;
    if (saved_field_count > MAX_FORM_FIELDS) saved_field_count = MAX_FORM_FIELDS;
    for (int fi = 0; fi < saved_field_count; fi++)
        saved_fields[fi] = T->form_fields[fi];
    T->form_field_count = 0;
    char *page_buf = T->page_buf;
    int page_len = T->page_len;

    /* Initialize style stack */
    style_depth = 0;
    mem_set(&style_stack[0], 0, sizeof(struct render_style));
    style_stack[0].color = C_TEXT;
    style_stack[0].bg_color = C_NONE;

    int draw_x = 8;
    int draw_y = 4;
    int max_x = BRW_W - 16;
    int heading = 0;
    bool in_link = false;
    bool in_list = false;
    bool at_li_start = false;
    bool in_style_tag = false;
    bool last_was_space = true;
    bool line_start = true;
    char current_href[ADDR_MAX + 1];
    int link_start_x = 0, link_start_y = 0;
    current_href[0] = '\0';

    int pos = 0;
    while (pos < page_len) {
        if (page_buf[pos] == '<') {
            /* HTML comment: <!-- ... --> */
            if (pos + 3 < page_len && page_buf[pos+1] == '!' &&
                page_buf[pos+2] == '-' && page_buf[pos+3] == '-') {
                pos += 4;
                while (pos + 2 < page_len) {
                    if (page_buf[pos] == '-' && page_buf[pos+1] == '-' && page_buf[pos+2] == '>') {
                        pos += 3;
                        break;
                    }
                    pos++;
                }
                continue;
            }

            pos++;
            bool closing = false;
            if (pos < page_len && page_buf[pos] == '/') { closing = true; pos++; }

            char tag_full[128];
            int tf = 0;
            while (pos < page_len && page_buf[pos] != '>' && tf < 127)
                tag_full[tf++] = page_buf[pos++];
            tag_full[tf] = '\0';
            if (pos < page_len && page_buf[pos] == '>') pos++;

            char tag_name[16];
            int tn = 0;
            for (int i = 0; i < tf && tag_full[i] != ' ' && tn < 15; i++)
                tag_name[tn++] = tag_full[i];
            tag_name[tn] = '\0';

            if (tag_eq(tag_name, tn, "style")) {
                in_style_tag = !closing;
                continue;
            }
            if (in_style_tag) continue;

            /* Skip <script> block content */
            if (tag_eq(tag_name, tn, "script")) {
                if (!closing) {
                    while (pos < page_len - 9) {
                        if (page_buf[pos] == '<' && page_buf[pos+1] == '/' &&
                            to_lower(page_buf[pos+2]) == 's' && to_lower(page_buf[pos+3]) == 'c' &&
                            to_lower(page_buf[pos+4]) == 'r' && to_lower(page_buf[pos+5]) == 'i' &&
                            to_lower(page_buf[pos+6]) == 'p' && to_lower(page_buf[pos+7]) == 't') {
                            while (pos < page_len && page_buf[pos] != '>') pos++;
                            if (pos < page_len) pos++;
                            break;
                        }
                        pos++;
                    }
                }
                continue;
            }

            if (!closing) {
                style_push();

                char class_val[32];
                extract_attr(tag_full, tf, "class", class_val, 32);
                apply_css_for_tag(tag_name, class_val, T->css_rules, T->css_rule_count);

                char inline_style[128];
                extract_attr(tag_full, tf, "style", inline_style, 128);
                if (inline_style[0])
                    apply_css_props(inline_style, str_len(inline_style), cur_style());

                if (cur_style()->display_none) {
                    /* Skip content - we still push/pop but don't render */
                }
            }

            bool hidden = cur_style()->display_none;

            if (!hidden) {
                bool is_block = false;
                if (tag_eq(tag_name, tn, "h1")) {
                    if (!closing) { heading = 1; draw_y += 8; draw_x = 8 + cur_style()->padding_left; }
                    else { heading = 0; draw_y += 20; draw_x = 8; }
                    is_block = true;
                } else if (tag_eq(tag_name, tn, "h2")) {
                    if (!closing) { heading = 2; draw_y += 6; draw_x = 8 + cur_style()->padding_left; }
                    else { heading = 0; draw_y += 18; draw_x = 8; }
                    is_block = true;
                } else if (tag_eq(tag_name, tn, "h3") || tag_eq(tag_name, tn, "h4") ||
                           tag_eq(tag_name, tn, "h5") || tag_eq(tag_name, tn, "h6")) {
                    if (!closing) { heading = 3; draw_y += 4; draw_x = 8 + cur_style()->padding_left; }
                    else { heading = 0; draw_y += 16; draw_x = 8; }
                    is_block = true;
                } else if (tag_eq(tag_name, tn, "p") || tag_eq(tag_name, tn, "div") ||
                           tag_eq(tag_name, tn, "article") || tag_eq(tag_name, tn, "section") ||
                           tag_eq(tag_name, tn, "aside") || tag_eq(tag_name, tn, "footer") ||
                           tag_eq(tag_name, tn, "header") || tag_eq(tag_name, tn, "nav")) {
                    if (!closing) { draw_y += 6; draw_x = 8 + cur_style()->padding_left; }
                    else { draw_y += 8; draw_x = 8; }
                    is_block = true;
                } else if (tag_eq(tag_name, tn, "blockquote")) {
                    if (!closing) { draw_y += 4; draw_x = 32 + cur_style()->padding_left; }
                    else { draw_y += 4; draw_x = 8; }
                    is_block = true;
                } else if (tag_eq(tag_name, tn, "pre") || tag_eq(tag_name, tn, "code")) {
                    if (!closing) { cur_style()->bold = true; }
                } else if (tag_eq(tag_name, tn, "b") || tag_eq(tag_name, tn, "strong")) {
                    if (!closing) cur_style()->bold = true;
                } else if (tag_eq(tag_name, tn, "i") || tag_eq(tag_name, tn, "em") ||
                           tag_eq(tag_name, tn, "abbr")) {
                    if (!closing) cur_style()->italic = true;
                } else if (tag_eq(tag_name, tn, "u")) {
                    if (!closing) cur_style()->underline = true;
                } else if (tag_eq(tag_name, tn, "sub") || tag_eq(tag_name, tn, "sup")) {
                    if (!closing) cur_style()->italic = true;
                } else if (tag_eq(tag_name, tn, "br")) {
                    draw_y += 16;
                    draw_x = 8 + cur_style()->padding_left;
                    is_block = true;
                } else if (tag_eq(tag_name, tn, "hr")) {
                    draw_y += 4;
                    int rule_y = CONTENT_Y + draw_y - T->scroll_y;
                    if (rule_y >= CONTENT_Y && rule_y < CONTENT_Y + CONTENT_H)
                        brw_rect(buf, cw, ch, 8, rule_y, BRW_W - 16, 1, C_HR);
                    draw_y += 8;
                    draw_x = 8;
                    is_block = true;
                } else if (tag_eq(tag_name, tn, "a")) {
                    if (!closing) {
                        in_link = true;
                        extract_attr(tag_full, tf, "href", current_href, ADDR_MAX);
                        link_start_x = draw_x;
                        link_start_y = draw_y;
                    } else {
                        if (in_link && T->link_count < MAX_LINKS && current_href[0]) {
                            T->links[T->link_count].x = link_start_x;
                            T->links[T->link_count].y = link_start_y;
                            T->links[T->link_count].w = draw_x - link_start_x;
                            T->links[T->link_count].h = 16;
                            str_ncopy(T->links[T->link_count].href, current_href, ADDR_MAX);
                            T->link_count++;
                        }
                        in_link = false;
                        current_href[0] = '\0';
                    }
                } else if (tag_eq(tag_name, tn, "ul") || tag_eq(tag_name, tn, "ol") ||
                           tag_eq(tag_name, tn, "dl")) {
                    if (!closing) { in_list = true; draw_y += 4; }
                    else { in_list = false; draw_y += 4; }
                    is_block = true;
                } else if (tag_eq(tag_name, tn, "li") || tag_eq(tag_name, tn, "dd")) {
                    if (!closing) {
                        draw_y += 2;
                        draw_x = (in_list ? 24 : 8) + cur_style()->padding_left;
                        at_li_start = true;
                    } else {
                        draw_y += 16; draw_x = 8;
                    }
                    is_block = true;
                } else if (tag_eq(tag_name, tn, "dt")) {
                    if (!closing) { draw_y += 2; draw_x = (in_list ? 16 : 8); cur_style()->bold = true; }
                    is_block = true;
                } else if (tag_eq(tag_name, tn, "table") || tag_eq(tag_name, tn, "thead") ||
                           tag_eq(tag_name, tn, "tbody")) {
                    if (!closing) { draw_y += 4; draw_x = 8; } else { draw_y += 4; }
                    is_block = true;
                } else if (tag_eq(tag_name, tn, "tr")) {
                    if (!closing) { draw_x = 8; } else { draw_y += 16; draw_x = 8; }
                    is_block = true;
                } else if (tag_eq(tag_name, tn, "td") || tag_eq(tag_name, tn, "th")) {
                    if (!closing) draw_x += 8; else draw_x += 16;
                } else if (tag_eq(tag_name, tn, "input") && !closing) {
                    /* Self-closing: create form field */
                    if (T->form_field_count < MAX_FORM_FIELDS) {
                        struct form_field *ff = &T->form_fields[T->form_field_count];
                        mem_set(ff, 0, sizeof(*ff));
                        /* Restore user data from previous frame */
                        if (T->form_field_count < saved_field_count) {
                            struct form_field *sf = &saved_fields[T->form_field_count];
                            mem_copy(ff->value, sf->value, FORM_VAL_MAX);
                            ff->value_len = sf->value_len;
                            ff->cursor = sf->cursor;
                            ff->checked = sf->checked;
                            ff->selected_option = sf->selected_option;
                            ff->dropdown_open = sf->dropdown_open;
                        }
                        char type_val[16];
                        extract_attr(tag_full, tf, "type", type_val, 16);
                        extract_attr(tag_full, tf, "name", ff->name, 32);
                        extract_attr(tag_full, tf, "onclick", ff->onclick, 64);
                        if (T->form_field_count >= saved_field_count) {
                            /* First time: read value from HTML */
                            extract_attr(tag_full, tf, "value", ff->value, FORM_VAL_MAX);
                            ff->value_len = str_len(ff->value);
                        }
                        extract_attr(tag_full, tf, "placeholder", ff->placeholder, 32);
                        if (ci_eq(type_val, "checkbox")) {
                            ff->type = FORM_CHECKBOX;
                            if (T->form_field_count >= saved_field_count) {
                                char chk[8];
                                extract_attr(tag_full, tf, "checked", chk, 8);
                                ff->checked = (chk[0] != '\0');
                            }
                            ff->x = draw_x; ff->y = draw_y;
                            ff->w = 16; ff->h = 16;
                            /* Render checkbox */
                            int sy = CONTENT_Y + draw_y - T->scroll_y;
                            if (sy >= CONTENT_Y && sy + 16 <= CONTENT_Y + CONTENT_H) {
                                bool focused = (T->focused_field == T->form_field_count);
                                color_t box_bg = COLOR_WHITE;
                                color_t box_border = focused ? COLOR_RGB(0, 80, 200) : COLOR_RGB(150, 150, 150);
                                brw_rect(buf, cw, ch, draw_x, sy, 14, 14, box_bg);
                                brw_rect(buf, cw, ch, draw_x, sy, 14, 1, box_border);
                                brw_rect(buf, cw, ch, draw_x, sy + 13, 14, 1, box_border);
                                brw_rect(buf, cw, ch, draw_x, sy, 1, 14, box_border);
                                brw_rect(buf, cw, ch, draw_x + 13, sy, 1, 14, box_border);
                                if (ff->checked) {
                                    brw_char_transparent(buf, cw, ch, draw_x + 3, sy - 1, 'X', COLOR_RGB(0, 120, 0));
                                }
                            }
                            draw_x += 18;
                            T->form_field_count++;
                        } else if (ci_eq(type_val, "password")) {
                            ff->type = FORM_PASSWORD;
                            ff->cursor = ff->value_len;
                            int fw = 160;
                            ff->x = draw_x; ff->y = draw_y;
                            ff->w = fw; ff->h = 18;
                            int sy = CONTENT_Y + draw_y - T->scroll_y;
                            if (sy >= CONTENT_Y && sy + 18 <= CONTENT_Y + CONTENT_H) {
                                bool focused = (T->focused_field == T->form_field_count);
                                color_t bdr = focused ? COLOR_RGB(0, 80, 200) : COLOR_RGB(180, 180, 180);
                                brw_rect(buf, cw, ch, draw_x, sy, fw, 18, COLOR_WHITE);
                                brw_rect(buf, cw, ch, draw_x, sy, fw, 1, bdr);
                                brw_rect(buf, cw, ch, draw_x, sy + 17, fw, 1, bdr);
                                brw_rect(buf, cw, ch, draw_x, sy, 1, 18, bdr);
                                brw_rect(buf, cw, ch, draw_x + fw - 1, sy, 1, 18, bdr);
                                /* Draw asterisks */
                                int tx = draw_x + 4;
                                for (int ci2 = 0; ci2 < ff->value_len && tx + 8 < draw_x + fw - 4; ci2++) {
                                    brw_char_transparent(buf, cw, ch, tx, sy + 1, '*', C_ADDR_FG);
                                    tx += 8;
                                }
                                if (focused && (timer_get_ticks() / 40) & 1) {
                                    brw_rect(buf, cw, ch, draw_x + 4 + ff->cursor * 8, sy + 2, 2, 14, COLOR_RGB(0, 80, 200));
                                }
                            }
                            draw_x += fw + 4;
                            T->form_field_count++;
                        } else {
                            /* Default: text input */
                            ff->type = FORM_INPUT;
                            ff->cursor = ff->value_len;
                            int fw = 160;
                            ff->x = draw_x; ff->y = draw_y;
                            ff->w = fw; ff->h = 18;
                            int sy = CONTENT_Y + draw_y - T->scroll_y;
                            if (sy >= CONTENT_Y && sy + 18 <= CONTENT_Y + CONTENT_H) {
                                bool focused = (T->focused_field == T->form_field_count);
                                color_t bdr = focused ? COLOR_RGB(0, 80, 200) : COLOR_RGB(180, 180, 180);
                                brw_rect(buf, cw, ch, draw_x, sy, fw, 18, COLOR_WHITE);
                                brw_rect(buf, cw, ch, draw_x, sy, fw, 1, bdr);
                                brw_rect(buf, cw, ch, draw_x, sy + 17, fw, 1, bdr);
                                brw_rect(buf, cw, ch, draw_x, sy, 1, 18, bdr);
                                brw_rect(buf, cw, ch, draw_x + fw - 1, sy, 1, 18, bdr);
                                /* Draw text or placeholder */
                                if (ff->value_len > 0) {
                                    ff->value[ff->value_len] = '\0';
                                    int tx = draw_x + 4;
                                    int max_chars = (fw - 8) / 8;
                                    int start = 0;
                                    if (ff->value_len > max_chars) start = ff->value_len - max_chars;
                                    for (int ci2 = start; ci2 < ff->value_len && tx + 8 < draw_x + fw - 4; ci2++) {
                                        brw_char_transparent(buf, cw, ch, tx, sy + 1, ff->value[ci2], C_ADDR_FG);
                                        tx += 8;
                                    }
                                } else if (ff->placeholder[0]) {
                                    brw_text(buf, cw, ch, draw_x + 4, sy + 1, ff->placeholder, COLOR_RGB(160, 160, 160), COLOR_WHITE);
                                }
                                if (focused && (timer_get_ticks() / 40) & 1) {
                                    int cursor_x = draw_x + 4 + ff->cursor * 8;
                                    if (ff->value_len > (fw - 8) / 8)
                                        cursor_x = draw_x + 4 + (ff->cursor - (ff->value_len - (fw - 8) / 8)) * 8;
                                    brw_rect(buf, cw, ch, cursor_x, sy + 2, 2, 14, COLOR_RGB(0, 80, 200));
                                }
                            }
                            draw_x += fw + 4;
                            T->form_field_count++;
                        }
                    }
                } else if (tag_eq(tag_name, tn, "button")) {
                    if (!closing) {
                        /* Collect button text from inner content until </button> */
                        char btn_text[32];
                        int bti = 0;
                        while (pos < page_len && bti < 31) {
                            if (page_buf[pos] == '<') {
                                /* Check for </button> */
                                if (pos + 8 < page_len && page_buf[pos+1] == '/' &&
                                    to_lower(page_buf[pos+2]) == 'b' && to_lower(page_buf[pos+3]) == 'u' &&
                                    to_lower(page_buf[pos+4]) == 't' && to_lower(page_buf[pos+5]) == 't' &&
                                    to_lower(page_buf[pos+6]) == 'o' && to_lower(page_buf[pos+7]) == 'n') {
                                    while (pos < page_len && page_buf[pos] != '>') pos++;
                                    if (pos < page_len) pos++;
                                    break;
                                }
                                /* Skip other tags */
                                while (pos < page_len && page_buf[pos] != '>') pos++;
                                if (pos < page_len) pos++;
                                continue;
                            }
                            if (page_buf[pos] != '\n' && page_buf[pos] != '\r')
                                btn_text[bti++] = page_buf[pos];
                            pos++;
                        }
                        btn_text[bti] = '\0';
                        /* Trim whitespace */
                        while (bti > 0 && btn_text[bti-1] == ' ') btn_text[--bti] = '\0';
                        int bs = 0;
                        while (btn_text[bs] == ' ') bs++;

                        if (T->form_field_count < MAX_FORM_FIELDS) {
                            struct form_field *ff = &T->form_fields[T->form_field_count];
                            mem_set(ff, 0, sizeof(*ff));
                            ff->type = FORM_BUTTON;
                            str_ncopy(ff->value, btn_text + bs, FORM_VAL_MAX - 1);
                            ff->value_len = str_len(ff->value);
                            int btn_w = ff->value_len * 8 + 16;
                            if (btn_w < 40) btn_w = 40;
                            ff->x = draw_x; ff->y = draw_y;
                            ff->w = btn_w; ff->h = 20;
                            extract_attr(tag_full, tf, "name", ff->name, 32);
                            extract_attr(tag_full, tf, "onclick", ff->onclick, 64);

                            int sy = CONTENT_Y + draw_y - T->scroll_y;
                            if (sy >= CONTENT_Y && sy + 20 <= CONTENT_Y + CONTENT_H) {
                                bool focused = (T->focused_field == T->form_field_count);
                                color_t btn_bg = focused ? COLOR_RGB(180, 185, 200) : C_BTN_BG;
                                brw_rect(buf, cw, ch, draw_x, sy, btn_w, 20, btn_bg);
                                /* Border */
                                brw_rect(buf, cw, ch, draw_x, sy, btn_w, 1, COLOR_RGB(160, 160, 170));
                                brw_rect(buf, cw, ch, draw_x, sy + 19, btn_w, 1, COLOR_RGB(160, 160, 170));
                                brw_rect(buf, cw, ch, draw_x, sy, 1, 20, COLOR_RGB(160, 160, 170));
                                brw_rect(buf, cw, ch, draw_x + btn_w - 1, sy, 1, 20, COLOR_RGB(160, 160, 170));
                                brw_text(buf, cw, ch, draw_x + 8, sy + 2, ff->value, C_BTN_FG, btn_bg);
                            }
                            draw_x += btn_w + 4;
                            T->form_field_count++;
                        }
                    }
                } else if (tag_eq(tag_name, tn, "textarea")) {
                    if (!closing) {
                        if (T->form_field_count < MAX_FORM_FIELDS) {
                            struct form_field *ff = &T->form_fields[T->form_field_count];
                            mem_set(ff, 0, sizeof(*ff));
                            ff->type = FORM_TEXTAREA;
                            extract_attr(tag_full, tf, "name", ff->name, 32);
                            /* Restore or collect initial text */
                            bool ta_restored = false;
                            if (T->form_field_count < saved_field_count) {
                                struct form_field *sf = &saved_fields[T->form_field_count];
                                mem_copy(ff->value, sf->value, FORM_VAL_MAX);
                                ff->value_len = sf->value_len;
                                ff->cursor = sf->cursor;
                                ta_restored = true;
                            }
                            /* Skip inner text until </textarea> */
                            while (pos < page_len) {
                                if (page_buf[pos] == '<' && pos + 10 < page_len &&
                                    page_buf[pos+1] == '/' &&
                                    to_lower(page_buf[pos+2]) == 't' && to_lower(page_buf[pos+3]) == 'e' &&
                                    to_lower(page_buf[pos+4]) == 'x' && to_lower(page_buf[pos+5]) == 't' &&
                                    to_lower(page_buf[pos+6]) == 'a' && to_lower(page_buf[pos+7]) == 'r' &&
                                    to_lower(page_buf[pos+8]) == 'e' && to_lower(page_buf[pos+9]) == 'a') {
                                    while (pos < page_len && page_buf[pos] != '>') pos++;
                                    if (pos < page_len) pos++;
                                    break;
                                }
                                if (!ta_restored && ff->value_len < FORM_VAL_MAX - 1) {
                                    ff->value[ff->value_len++] = page_buf[pos];
                                }
                                pos++;
                            }
                            ff->value[ff->value_len] = '\0';
                            if (!ta_restored) ff->cursor = ff->value_len;

                            int fw = 200, fh = 60;
                            ff->x = draw_x; ff->y = draw_y;
                            ff->w = fw; ff->h = fh;
                            int sy = CONTENT_Y + draw_y - T->scroll_y;
                            if (sy >= CONTENT_Y && sy + fh <= CONTENT_Y + CONTENT_H) {
                                bool focused = (T->focused_field == T->form_field_count);
                                color_t bdr = focused ? COLOR_RGB(0, 80, 200) : COLOR_RGB(180, 180, 180);
                                brw_rect(buf, cw, ch, draw_x, sy, fw, fh, COLOR_WHITE);
                                brw_rect(buf, cw, ch, draw_x, sy, fw, 1, bdr);
                                brw_rect(buf, cw, ch, draw_x, sy + fh - 1, fw, 1, bdr);
                                brw_rect(buf, cw, ch, draw_x, sy, 1, fh, bdr);
                                brw_rect(buf, cw, ch, draw_x + fw - 1, sy, 1, fh, bdr);
                                /* Draw text */
                                int tx = draw_x + 4, ty = sy + 2;
                                for (int ci2 = 0; ci2 < ff->value_len; ci2++) {
                                    if (ff->value[ci2] == '\n') { tx = draw_x + 4; ty += 16; continue; }
                                    if (tx + 8 > draw_x + fw - 4) { tx = draw_x + 4; ty += 16; }
                                    if (ty + 16 > sy + fh - 2) break;
                                    brw_char_transparent(buf, cw, ch, tx, ty, ff->value[ci2], C_ADDR_FG);
                                    tx += 8;
                                }
                                if (focused && (timer_get_ticks() / 40) & 1) {
                                    brw_rect(buf, cw, ch, tx, ty + 1, 2, 14, COLOR_RGB(0, 80, 200));
                                }
                            }
                            draw_y += fh + 4;
                            draw_x = 8 + cur_style()->padding_left;
                            T->form_field_count++;
                            is_block = true;
                        }
                    }
                } else if (tag_eq(tag_name, tn, "select")) {
                    if (!closing) {
                        if (T->form_field_count < MAX_FORM_FIELDS) {
                            struct form_field *ff = &T->form_fields[T->form_field_count];
                            mem_set(ff, 0, sizeof(*ff));
                            ff->type = FORM_SELECT;
                            extract_attr(tag_full, tf, "name", ff->name, 32);
                            /* Restore user selection from previous frame */
                            if (T->form_field_count < saved_field_count) {
                                struct form_field *sf = &saved_fields[T->form_field_count];
                                ff->selected_option = sf->selected_option;
                                ff->dropdown_open = sf->dropdown_open;
                            }
                            /* Parse <option> children */
                            while (pos < page_len && ff->option_count < FORM_OPT_MAX) {
                                /* Find next <option> or </select> */
                                while (pos < page_len && page_buf[pos] != '<') pos++;
                                if (pos >= page_len) break;
                                if (pos + 8 < page_len && page_buf[pos+1] == '/' &&
                                    to_lower(page_buf[pos+2]) == 's' && to_lower(page_buf[pos+3]) == 'e' &&
                                    to_lower(page_buf[pos+4]) == 'l' && to_lower(page_buf[pos+5]) == 'e' &&
                                    to_lower(page_buf[pos+6]) == 'c' && to_lower(page_buf[pos+7]) == 't') {
                                    while (pos < page_len && page_buf[pos] != '>') pos++;
                                    if (pos < page_len) pos++;
                                    break;
                                }
                                if (pos + 6 < page_len &&
                                    to_lower(page_buf[pos+1]) == 'o' && to_lower(page_buf[pos+2]) == 'p' &&
                                    to_lower(page_buf[pos+3]) == 't' && to_lower(page_buf[pos+4]) == 'i' &&
                                    to_lower(page_buf[pos+5]) == 'o' && to_lower(page_buf[pos+6]) == 'n') {
                                    /* Skip to > */
                                    while (pos < page_len && page_buf[pos] != '>') pos++;
                                    if (pos < page_len) pos++;
                                    /* Collect text until </option> or next < */
                                    int oi = 0;
                                    while (pos < page_len && page_buf[pos] != '<' && oi < 31) {
                                        if (page_buf[pos] != '\n' && page_buf[pos] != '\r')
                                            ff->options[ff->option_count][oi++] = page_buf[pos];
                                        pos++;
                                    }
                                    ff->options[ff->option_count][oi] = '\0';
                                    /* Trim */
                                    while (oi > 0 && ff->options[ff->option_count][oi-1] == ' ')
                                        ff->options[ff->option_count][--oi] = '\0';
                                    ff->option_count++;
                                    /* Skip </option> if present */
                                    if (pos < page_len && page_buf[pos] == '<' &&
                                        pos + 1 < page_len && page_buf[pos+1] == '/') {
                                        while (pos < page_len && page_buf[pos] != '>') pos++;
                                        if (pos < page_len) pos++;
                                    }
                                    continue;
                                }
                                /* Skip unknown tag */
                                while (pos < page_len && page_buf[pos] != '>') pos++;
                                if (pos < page_len) pos++;
                            }

                            /* Render the select dropdown */
                            int sw = 120;
                            ff->x = draw_x; ff->y = draw_y;
                            ff->w = sw; ff->h = 18;
                            int sy = CONTENT_Y + draw_y - T->scroll_y;
                            if (sy >= CONTENT_Y && sy + 18 <= CONTENT_Y + CONTENT_H) {
                                bool focused = (T->focused_field == T->form_field_count);
                                color_t bdr = focused ? COLOR_RGB(0, 80, 200) : COLOR_RGB(180, 180, 180);
                                brw_rect(buf, cw, ch, draw_x, sy, sw, 18, COLOR_WHITE);
                                brw_rect(buf, cw, ch, draw_x, sy, sw, 1, bdr);
                                brw_rect(buf, cw, ch, draw_x, sy + 17, sw, 1, bdr);
                                brw_rect(buf, cw, ch, draw_x, sy, 1, 18, bdr);
                                brw_rect(buf, cw, ch, draw_x + sw - 1, sy, 1, 18, bdr);
                                /* Arrow indicator */
                                brw_rect(buf, cw, ch, draw_x + sw - 16, sy, 1, 18, bdr);
                                brw_char_transparent(buf, cw, ch, draw_x + sw - 12, sy + 1, 'v', C_ADDR_FG);
                                /* Selected option text */
                                if (ff->option_count > 0 && ff->selected_option < ff->option_count) {
                                    brw_text(buf, cw, ch, draw_x + 4, sy + 1,
                                             ff->options[ff->selected_option], C_ADDR_FG, COLOR_WHITE);
                                }
                                /* Draw dropdown options if open */
                                if (ff->dropdown_open && ff->option_count > 0) {
                                    int dy = sy + 18;
                                    for (int oi = 0; oi < ff->option_count; oi++) {
                                        if (dy + 16 > CONTENT_Y + CONTENT_H) break;
                                        color_t opt_bg = (oi == ff->selected_option) ?
                                            COLOR_RGB(0, 80, 200) : COLOR_WHITE;
                                        color_t opt_fg = (oi == ff->selected_option) ?
                                            COLOR_WHITE : C_ADDR_FG;
                                        brw_rect(buf, cw, ch, draw_x, dy, sw, 16, opt_bg);
                                        brw_rect(buf, cw, ch, draw_x, dy, 1, 16, bdr);
                                        brw_rect(buf, cw, ch, draw_x + sw - 1, dy, 1, 16, bdr);
                                        if (oi == ff->option_count - 1)
                                            brw_rect(buf, cw, ch, draw_x, dy + 15, sw, 1, bdr);
                                        brw_text(buf, cw, ch, draw_x + 4, dy, ff->options[oi], opt_fg, opt_bg);
                                        dy += 16;
                                    }
                                }
                            }
                            draw_x += sw + 4;
                            T->form_field_count++;
                        }
                    }
                } else if (tag_eq(tag_name, tn, "option")) {
                    /* Options are parsed inside <select> handler, skip stray ones */
                    if (!closing) {
                        while (pos < page_len && page_buf[pos] != '<') pos++;
                    }
                } else if (tag_eq(tag_name, tn, "form")) {
                    /* <form> is a block container */
                    if (!closing) { draw_y += 4; draw_x = 8 + cur_style()->padding_left; }
                    else { draw_y += 4; draw_x = 8; }
                    is_block = true;
                } else if (tag_eq(tag_name, tn, "img") && !closing) {
                    /* Self-closing <img> tag */
                    char img_src[ADDR_MAX + 1];
                    char img_alt[32];
                    extract_attr(tag_full, tf, "src", img_src, ADDR_MAX);
                    extract_attr(tag_full, tf, "alt", img_alt, 32);
                    bool img_ok = false;
                    int img_w = 0, img_h = 0;
                    if (img_src[0]) {
                        int fidx = fs_find(img_src);
                        if (fidx >= 0) {
                            struct fs_node *fnode = fs_get_node(fidx);
                            if (fnode && fnode->type == FS_FILE && fnode->data) {
                                img_ok = pic_decode(fnode->data, (int)fnode->size, &img_w, &img_h);
                            }
                        }
                    }
                    if (img_ok && img_w > 0 && img_h > 0) {
                        brw_blit_pic(buf, cw, ch, draw_x, draw_y, img_w, img_h, T->scroll_y);
                        draw_y += img_h + 4;
                    } else {
                        /* Show [alt] fallback */
                        int sy = CONTENT_Y + draw_y - T->scroll_y;
                        if (sy >= CONTENT_Y && sy + 16 <= CONTENT_Y + CONTENT_H) {
                            brw_char_transparent(buf, cw, ch, draw_x, sy, '[', COLOR_RGB(180, 60, 60));
                            draw_x += 8;
                            const char *alt = img_alt[0] ? img_alt : "img";
                            while (*alt) {
                                brw_char_transparent(buf, cw, ch, draw_x, sy, *alt, COLOR_RGB(180, 60, 60));
                                draw_x += 8;
                                alt++;
                            }
                            brw_char_transparent(buf, cw, ch, draw_x, sy, ']', COLOR_RGB(180, 60, 60));
                            draw_x += 8;
                        }
                        draw_y += 20;
                    }
                    draw_x = 8 + cur_style()->padding_left;
                    style_pop(); /* <img> is void — pop the style pushed for it */
                    is_block = true;
                    last_was_space = true;
                    line_start = true;
                }
                if (is_block) {
                    if (!closing) {
                        draw_y += cur_style()->margin_top;
                        /* Draw top border if element has border */
                        if (!hidden && cur_style()->has_border) {
                            int bw = cur_style()->border_width;
                            if (bw < 1) bw = 1;
                            if (bw > 4) bw = 4;
                            color_t bc = cur_style()->border_color;
                            int bx = 8 + cur_style()->margin_left;
                            int by = CONTENT_Y + draw_y - T->scroll_y;
                            int bwidth = BRW_W - 16 - cur_style()->margin_left - cur_style()->margin_right;
                            if (by >= CONTENT_Y && by < CONTENT_Y + CONTENT_H)
                                brw_rect(buf, cw, ch, bx, by, bwidth, bw, bc);
                            draw_y += bw + 2;
                        }
                        draw_y += cur_style()->padding_top;
                        draw_x += cur_style()->margin_left;
                        /* Apply width constraint */
                        if (cur_style()->has_width && cur_style()->width > 0 && cur_style()->width < max_x - draw_x)
                            max_x = draw_x + cur_style()->width;
                        else if (cur_style()->has_max_width && cur_style()->max_width > 0 && draw_x + cur_style()->max_width < max_x)
                            max_x = draw_x + cur_style()->max_width;
                    }
                    last_was_space = true;
                    line_start = true;
                }
            }

            if (closing) {
                if (!hidden && cur_style()->has_border_bottom) {
                    int rule_y = CONTENT_Y + draw_y - T->scroll_y;
                    if (rule_y >= CONTENT_Y && rule_y < CONTENT_Y + CONTENT_H)
                        brw_rect(buf, cw, ch, 8, rule_y, BRW_W - 16, 1, cur_style()->border_bottom_color);
                    draw_y += 2;
                }
                draw_y += cur_style()->padding_bottom;
                /* Draw border box if element has border */
                if (!hidden && cur_style()->has_border) {
                    /* We approximate: draw border lines at current draw_y position */
                    int bw = cur_style()->border_width;
                    if (bw < 1) bw = 1;
                    if (bw > 4) bw = 4;
                    color_t bc = cur_style()->border_color;
                    int bx = 8 + cur_style()->margin_left;
                    int by_end = CONTENT_Y + draw_y - T->scroll_y;
                    int bwidth = BRW_W - 16 - cur_style()->margin_left - cur_style()->margin_right;
                    /* Bottom border */
                    if (by_end >= CONTENT_Y && by_end < CONTENT_Y + CONTENT_H)
                        brw_rect(buf, cw, ch, bx, by_end, bwidth, bw, bc);
                    draw_y += bw + 2;
                }
                draw_y += cur_style()->margin_bottom;
                /* Restore max_x */
                max_x = BRW_W - 16;
                style_pop();
            }
            continue;
        }

        if (in_style_tag) { pos++; continue; }
        if (cur_style()->display_none) { pos++; continue; }

        char c = page_buf[pos];
        bool is_nbsp = false;

        /* Decode HTML entities */
        if (c == '&') {
            char decoded;
            int consumed = decode_entity(page_buf, pos, page_len, &decoded, &is_nbsp);
            if (consumed > 0) {
                c = decoded;
                pos += consumed;
            } else {
                pos++;
            }
        } else {
            pos++;
        }

        /* white-space: pre preserves newlines and tabs */
        if (cur_style()->white_space_pre) {
            if (c == '\n') {
                draw_y += cur_style()->line_height ? cur_style()->line_height : 16;
                draw_x = 8 + cur_style()->padding_left + cur_style()->margin_left;
                line_start = true;
                last_was_space = false;
                continue;
            }
            if (c == '\t') c = ' '; /* tab as space but don't collapse */
            /* don't collapse spaces in pre mode */
        } else {
            if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        }

        if (c == ' ' && !is_nbsp && !cur_style()->white_space_pre) {
            if (last_was_space || line_start) continue;
            last_was_space = true;
        } else {
            last_was_space = false;
        }

        struct render_style *st = cur_style();

        /* visibility: hidden — take up space but don't draw */
        if (st->visibility_hidden) {
            int char_w = 8 + st->letter_spacing;
            draw_x += char_w;
            if (draw_x + char_w > max_x) {
                draw_y += st->line_height ? st->line_height : 16;
                draw_x = 8 + st->padding_left + st->margin_left;
                line_start = true;
            }
            continue;
        }
        color_t fg;
        if (in_link) {
            fg = C_LINK;
        } else if (st->color != C_TEXT || heading || st->bold || st->italic) {
            if (st->color != C_TEXT) {
                fg = st->color;
            } else if (heading == 1) {
                fg = C_H1;
            } else if (heading == 2) {
                fg = C_H2;
            } else if (heading == 3) {
                fg = C_H3;
            } else if (st->bold) {
                fg = C_BOLD;
            } else if (st->italic) {
                fg = C_ITALIC;
            } else {
                fg = st->color;
            }
        } else {
            fg = st->color;
        }

        int char_w = 8 + st->letter_spacing;
        int line_h = st->line_height ? st->line_height : 16;

        if (c == ' ') {
            int ww = measure_word(page_buf, page_len, pos);
            if (ww > 0 && draw_x + char_w + ww > max_x) {
                draw_y += line_h;
                draw_x = (in_list ? 24 : 8) + st->padding_left + st->margin_left;
                line_start = true;
                last_was_space = true;
                continue;
            }
        }

        if (draw_x + char_w > max_x) {
            draw_y += line_h;
            draw_x = (in_list ? 24 : 8) + st->padding_left + st->margin_left;
            line_start = true;
        }

        if (line_start && (st->text_center || st->text_right)) {
            int base_x = draw_x;
            int lw = measure_line_width(page_buf, page_len, pos - 1, base_x, max_x);
            if (st->text_center)
                draw_x = base_x + (max_x - base_x - lw) / 2;
            else if (st->text_right)
                draw_x = max_x - lw;
            if (draw_x < base_x) draw_x = base_x;
        }
        line_start = false;

        if (at_li_start) {
            int bullet_sy = CONTENT_Y + draw_y - T->scroll_y;
            if (bullet_sy >= CONTENT_Y && bullet_sy + 16 <= CONTENT_Y + CONTENT_H) {
                if (st->list_style != 1) { /* 1 = none */
                    if (st->list_style == 3) /* square */
                        brw_rect(buf, cw, ch, draw_x - 12, bullet_sy + 5, 6, 6, C_BULLET);
                    else if (st->list_style == 2) { /* circle - outline only */
                        brw_rect(buf, cw, ch, draw_x - 12, bullet_sy + 5, 6, 1, C_BULLET);
                        brw_rect(buf, cw, ch, draw_x - 12, bullet_sy + 10, 6, 1, C_BULLET);
                        brw_rect(buf, cw, ch, draw_x - 12, bullet_sy + 5, 1, 6, C_BULLET);
                        brw_rect(buf, cw, ch, draw_x - 7, bullet_sy + 5, 1, 6, C_BULLET);
                    } else /* disc (default) */
                        brw_rect(buf, cw, ch, draw_x - 12, bullet_sy + 6, 4, 4, C_BULLET);
                }
            }
            at_li_start = false;
        }

        int screen_y = CONTENT_Y + draw_y - T->scroll_y;
        if (screen_y >= CONTENT_Y && screen_y + 16 <= CONTENT_Y + CONTENT_H) {
            if (st->bg_color != C_NONE)
                brw_rect(buf, cw, ch, draw_x, screen_y, char_w, 16, st->bg_color);

            brw_char_transparent(buf, cw, ch, draw_x, screen_y, c, fg);
            if (st->underline || in_link)
                brw_underline(buf, cw, ch, draw_x, screen_y, char_w, fg);
        }

        draw_x += char_w;
    }

    /* Scroll clamping */
    T->content_total_h = draw_y + 20;
    if (T->scroll_y > T->content_total_h - CONTENT_H) {
        if (T->content_total_h > CONTENT_H)
            T->scroll_y = T->content_total_h - CONTENT_H;
        else
            T->scroll_y = 0;
    }

    /* Status bar */
    brw_rect(buf, cw, ch, 0, BRW_H - STATUS_H, BRW_W, STATUS_H, C_STATUS_BG);
    brw_text(buf, cw, ch, 4, BRW_H - STATUS_H + 1, status_msg, C_STATUS_FG, C_STATUS_BG);

    /* Scrollbar */
    if (T->content_total_h > CONTENT_H) {
        int track_h = CONTENT_H - 4;
        int thumb_h = (CONTENT_H * track_h) / T->content_total_h;
        if (thumb_h < 10) thumb_h = 10;
        int thumb_y = CONTENT_Y + 2 + (T->scroll_y * (track_h - thumb_h)) / (T->content_total_h - CONTENT_H);
        brw_rect(buf, cw, ch, BRW_W - 6, CONTENT_Y + 2, 4, track_h, COLOR_RGB(210, 210, 215));
        brw_rect(buf, cw, ch, BRW_W - 6, thumb_y, 4, thumb_h, COLOR_RGB(160, 160, 170));
    }

    /* Alert overlay */
    if (alert_active) {
        /* Darken content area with checkerboard pattern */
        for (int py = CONTENT_Y; py < BRW_H - STATUS_H; py++) {
            for (int px = 0; px < cw; px++) {
                if ((px + py) & 1)
                    buf[py * cw + px] = COLOR_RGB(0, 0, 0);
            }
        }
        /* Centered alert box */
        int aw = 280, ah = 80;
        int ax = (BRW_W - aw) / 2;
        int ay = CONTENT_Y + (CONTENT_H - ah) / 2;
        brw_rect(buf, cw, ch, ax, ay, aw, ah, COLOR_WHITE);
        /* Border */
        brw_rect(buf, cw, ch, ax, ay, aw, 2, COLOR_RGB(60, 60, 120));
        brw_rect(buf, cw, ch, ax, ay + ah - 2, aw, 2, COLOR_RGB(60, 60, 120));
        brw_rect(buf, cw, ch, ax, ay, 2, ah, COLOR_RGB(60, 60, 120));
        brw_rect(buf, cw, ch, ax + aw - 2, ay, 2, ah, COLOR_RGB(60, 60, 120));
        /* Title bar */
        brw_rect(buf, cw, ch, ax + 2, ay + 2, aw - 4, 18, COLOR_RGB(60, 60, 120));
        brw_text(buf, cw, ch, ax + 8, ay + 3, "Alert", COLOR_WHITE, COLOR_RGB(60, 60, 120));
        /* Message text (word wrap within box) */
        int msg_x = ax + 12, msg_y = ay + 24;
        int msg_max_x = ax + aw - 12;
        const char *mp = alert_message;
        while (*mp) {
            if (msg_x + 8 > msg_max_x) { msg_x = ax + 12; msg_y += 16; }
            if (msg_y + 16 > ay + ah - 24) break;
            brw_char_transparent(buf, cw, ch, msg_x, msg_y, *mp, COLOR_RGB(30, 30, 30));
            msg_x += 8;
            mp++;
        }
        /* OK button */
        int ok_w = 48, ok_h = 18;
        int ok_x = ax + (aw - ok_w) / 2;
        int ok_y = ay + ah - ok_h - 6;
        brw_rect(buf, cw, ch, ok_x, ok_y, ok_w, ok_h, C_BTN_BG);
        brw_rect(buf, cw, ch, ok_x, ok_y, ok_w, 1, COLOR_RGB(160, 160, 170));
        brw_rect(buf, cw, ch, ok_x, ok_y + ok_h - 1, ok_w, 1, COLOR_RGB(160, 160, 170));
        brw_rect(buf, cw, ch, ok_x, ok_y, 1, ok_h, COLOR_RGB(160, 160, 170));
        brw_rect(buf, cw, ch, ok_x + ok_w - 1, ok_y, 1, ok_h, COLOR_RGB(160, 160, 170));
        brw_text(buf, cw, ch, ok_x + 12, ok_y + 1, "OK", C_BTN_FG, C_BTN_BG);
        /* Store OK button coords for click detection */
        alert_ok_x = ok_x; alert_ok_y = ok_y;
        alert_ok_w = ok_w; alert_ok_h = ok_h;
    }
}
