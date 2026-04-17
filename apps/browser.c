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
#define ADDR_X      40
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

/* Clickable link regions */
#define MAX_LINKS 32
struct link_region {
    int x, y, w, h;
    char href[ADDR_MAX + 1];
};

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
        } else if (ci_eq(prop, "display")) {
            st->display_none = ci_eq(val, "none");
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
    if (r->has_padding_left) st->padding_left = r->padding_left;
    if (r->has_text_align) { st->text_center = r->text_center; st->text_right = r->text_right; }
    if (r->has_display) st->display_none = r->display_none;
    if (r->has_border_bottom) {
        st->has_border_bottom = true;
        st->border_bottom_color = r->border_bottom_color;
    }
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
    struct css_rule css_rules[MAX_CSS_RULES];
    int css_rule_count;
    char history[HISTORY_MAX][ADDR_MAX + 1];
    int history_count;
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
    tab->scroll_y = 0;
    tab->link_count = 0;
    tab->hovered_link = -1;
    str_copy(status_msg, "Ready");
}

static void navigate_back(struct browser_tab *tab) {
    if (tab->history_count <= 0) return;
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
    tab->scroll_y = 0;
    tab->link_count = 0;
    tab->hovered_link = -1;
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
        if (k == KEY_UP)   { T->scroll_y -= 16; if (T->scroll_y < 0) T->scroll_y = 0; return; }
        if (k == KEY_DOWN) { T->scroll_y += 16; return; }
        if (k == KEY_PGUP) { T->scroll_y -= CONTENT_H; if (T->scroll_y < 0) T->scroll_y = 0; return; }
        if (k == KEY_PGDN) { T->scroll_y += CONTENT_H; return; }
        if (k == KEY_HOME) { T->scroll_y = 0; return; }
        if (k == KEY_END)  { T->scroll_y = T->content_total_h - CONTENT_H; if (T->scroll_y < 0) T->scroll_y = 0; return; }
        if (k == '\b') { navigate_back(T); return; }
        if (k == '\t') { addr_focused = true; return; }
        if (k == 12) { addr_focused = true; T->addr_len = 0; return; }
        return;
    }

    if (evt->type == EVT_MOUSE_DOWN) {
        int mx = evt->mouse_x - (win->x + BORDER_WIDTH);
        int my = evt->mouse_y - (win->y + TITLEBAR_HEIGHT);

        /* Toolbar: back button */
        if (mx >= 4 && mx < 34 && my >= 4 && my < 22) { navigate_back(T); return; }
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

        /* Content area: link clicks */
        if (my >= CONTENT_Y && my < CONTENT_Y + CONTENT_H) {
            int content_mx = mx;
            int content_my = my - CONTENT_Y + T->scroll_y;
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
    brw_rect(buf, cw, ch, 4, 4, 30, 18, C_BTN_BG);
    brw_text(buf, cw, ch, 8, 5, "<-", C_BTN_FG, C_BTN_BG);
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
                }
                if (is_block) {
                    if (!closing) draw_y += cur_style()->margin_top;
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
                draw_y += cur_style()->margin_bottom;
                style_pop();
            }
            continue;
        }

        if (in_style_tag) { pos++; continue; }
        if (cur_style()->display_none) { pos++; continue; }

        char c = page_buf[pos++];
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';

        if (c == ' ') {
            if (last_was_space || line_start) continue;
            last_was_space = true;
        } else {
            last_was_space = false;
        }

        struct render_style *st = cur_style();
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

        if (c == ' ') {
            int ww = measure_word(page_buf, page_len, pos);
            if (ww > 0 && draw_x + 8 + ww > max_x) {
                draw_y += 16;
                draw_x = (in_list ? 24 : 8) + st->padding_left;
                line_start = true;
                last_was_space = true;
                continue;
            }
        }

        if (draw_x + 8 > max_x) {
            draw_y += 16;
            draw_x = (in_list ? 24 : 8) + st->padding_left;
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
            if (bullet_sy >= CONTENT_Y && bullet_sy + 16 <= CONTENT_Y + CONTENT_H)
                brw_rect(buf, cw, ch, draw_x - 12, bullet_sy + 6, 4, 4, C_BULLET);
            at_li_start = false;
        }

        int screen_y = CONTENT_Y + draw_y - T->scroll_y;
        if (screen_y >= CONTENT_Y && screen_y + 16 <= CONTENT_Y + CONTENT_H) {
            if (st->bg_color != C_NONE)
                brw_rect(buf, cw, ch, draw_x, screen_y, 8, 16, st->bg_color);

            brw_char_transparent(buf, cw, ch, draw_x, screen_y, c, fg);
            if (st->underline || in_link)
                brw_underline(buf, cw, ch, draw_x, screen_y, 8, fg);
        }

        draw_x += 8;
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
}
