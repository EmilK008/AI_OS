/* ===========================================================================
 * Notepad - Windowed text editor with customization features
 *   Themes, color picker, find/replace, line numbers, word wrap toggle,
 *   word count, Ctrl+S save, Ctrl+F find, Ctrl+N new
 * =========================================================================== */
#include "notepad.h"
#include "window.h"
#include "framebuffer.h"
#include "font8x16.h"
#include "fs.h"
#include "string.h"
#include "keyboard.h"
#include "timer.h"

#define NP_W        460
#define NP_H        300
#define TOOLBAR_H   24
#define TOOLBAR2_Y  TOOLBAR_H
#define TOOLBAR2_H  22
#define HEADER_H    (TOOLBAR_H + TOOLBAR2_H)
#define STATUS_H    20
#define TEXT_PAD     4
#define GUTTER_W     32   /* line number gutter width when enabled */
#define BUF_SIZE    4000
#define FIND_MAX    40
#define REPLACE_MAX 40

/* Toolbar buttons - row 1 */
#define ROW1_BTN_COUNT 8
#define BTN_W      50
#define BTN_H      18
#define BTN_GAP    4

/* Modes */
#define MODE_EDIT    0
#define MODE_OPEN    1
#define MODE_SAVE    2
#define MODE_FIND    3
#define MODE_COLOR   4

/* Theme definition */
struct np_theme {
    const char *name;
    color_t text_bg;
    color_t text_fg;
    color_t toolbar_bg;
    color_t status_bg;
    color_t status_fg;
    color_t gutter_bg;
    color_t gutter_fg;
    color_t cursor_color;
    color_t highlight_bg;
    color_t btn_bg;
    color_t btn_fg;
};

#define THEME_COUNT 4
static const struct np_theme themes[THEME_COUNT] = {
    { /* Dark */
        "Dark",
        COLOR_RGB(20, 20, 28),     /* text_bg */
        COLOR_WHITE,               /* text_fg */
        COLOR_RGB(50, 50, 62),     /* toolbar_bg */
        COLOR_RGB(40, 40, 52),     /* status_bg */
        COLOR_RGB(180, 180, 180),  /* status_fg */
        COLOR_RGB(28, 28, 38),     /* gutter_bg */
        COLOR_RGB(80, 80, 100),    /* gutter_fg */
        COLOR_RGB(100, 200, 255),  /* cursor_color */
        COLOR_RGB(80, 60, 0),      /* highlight_bg (find match) */
        COLOR_RGB(60, 60, 78),     /* btn_bg */
        COLOR_WHITE,               /* btn_fg */
    },
    { /* Light */
        "Light",
        COLOR_RGB(245, 245, 240),  /* text_bg */
        COLOR_RGB(20, 20, 20),     /* text_fg */
        COLOR_RGB(210, 210, 215),  /* toolbar_bg */
        COLOR_RGB(220, 220, 225),  /* status_bg */
        COLOR_RGB(60, 60, 60),     /* status_fg */
        COLOR_RGB(230, 230, 235),  /* gutter_bg */
        COLOR_RGB(140, 140, 150),  /* gutter_fg */
        COLOR_RGB(0, 100, 200),    /* cursor_color */
        COLOR_RGB(255, 220, 80),   /* highlight_bg */
        COLOR_RGB(180, 180, 190),  /* btn_bg */
        COLOR_RGB(20, 20, 20),     /* btn_fg */
    },
    { /* Retro Green */
        "Retro",
        COLOR_RGB(0, 10, 0),       /* text_bg */
        COLOR_RGB(0, 220, 0),      /* text_fg */
        COLOR_RGB(0, 30, 0),       /* toolbar_bg */
        COLOR_RGB(0, 25, 0),       /* status_bg */
        COLOR_RGB(0, 180, 0),      /* status_fg */
        COLOR_RGB(0, 15, 0),       /* gutter_bg */
        COLOR_RGB(0, 100, 0),      /* gutter_fg */
        COLOR_RGB(0, 255, 0),      /* cursor_color */
        COLOR_RGB(0, 80, 0),       /* highlight_bg */
        COLOR_RGB(0, 50, 0),       /* btn_bg */
        COLOR_RGB(0, 220, 0),      /* btn_fg */
    },
    { /* Ocean Blue */
        "Ocean",
        COLOR_RGB(10, 20, 40),     /* text_bg */
        COLOR_RGB(180, 220, 255),  /* text_fg */
        COLOR_RGB(20, 40, 70),     /* toolbar_bg */
        COLOR_RGB(15, 30, 55),     /* status_bg */
        COLOR_RGB(140, 180, 220),  /* status_fg */
        COLOR_RGB(12, 25, 48),     /* gutter_bg */
        COLOR_RGB(60, 100, 150),   /* gutter_fg */
        COLOR_RGB(80, 180, 255),   /* cursor_color */
        COLOR_RGB(60, 80, 30),     /* highlight_bg */
        COLOR_RGB(30, 55, 90),     /* btn_bg */
        COLOR_RGB(180, 220, 255),  /* btn_fg */
    },
};

/* Color palette for custom color picker */
#define PALETTE_COLS  8
#define PALETTE_ROWS  4
#define PALETTE_COUNT (PALETTE_COLS * PALETTE_ROWS)
static const color_t palette[PALETTE_COUNT] = {
    COLOR_RGB(0,0,0),       COLOR_RGB(40,40,40),    COLOR_RGB(80,80,80),    COLOR_RGB(128,128,128),
    COLOR_RGB(180,180,180),  COLOR_RGB(220,220,220), COLOR_RGB(245,245,240), COLOR_RGB(255,255,255),
    COLOR_RGB(180,0,0),     COLOR_RGB(255,60,60),   COLOR_RGB(220,120,0),   COLOR_RGB(255,180,0),
    COLOR_RGB(200,200,0),   COLOR_RGB(255,255,80),  COLOR_RGB(0,160,0),     COLOR_RGB(0,220,0),
    COLOR_RGB(0,160,160),   COLOR_RGB(0,220,220),   COLOR_RGB(0,80,200),    COLOR_RGB(80,140,255),
    COLOR_RGB(100,0,200),   COLOR_RGB(180,80,255),  COLOR_RGB(200,0,120),   COLOR_RGB(255,80,180),
    COLOR_RGB(0,10,0),      COLOR_RGB(10,20,40),    COLOR_RGB(20,20,28),    COLOR_RGB(30,30,38),
    COLOR_RGB(245,245,240), COLOR_RGB(180,220,255), COLOR_RGB(0,220,0),     COLOR_RGB(100,200,255),
};

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

/* Theme/customization state */
static int  current_theme = 0;
static bool custom_colors = false;
static color_t custom_text_fg = COLOR_WHITE;
static color_t custom_text_bg = COLOR_RGB(20, 20, 28);

/* Feature toggles */
static bool show_line_numbers = false;
static bool word_wrap = true;

/* Find/replace state */
static char find_buf[FIND_MAX + 1];
static int  find_len = 0;
static char replace_buf[REPLACE_MAX + 1];
static int  replace_len = 0;
static int  find_match_pos = -1;  /* position of current match in text_buf */
static bool find_field_active = true; /* true = editing find, false = editing replace */

/* File picker state (for MODE_OPEN) */
static int  picker_entries[MAX_CHILDREN];
static int  picker_count = 0;
static int  picker_sel = 0;
static int  picker_scroll = 0;
#define PICKER_VISIBLE  10  /* max visible rows in picker */

/* ---- HTML Autocomplete ---- */
#define AC_MODE_TAG    0
#define AC_MODE_ATTR   1
#define AC_MODE_CLOSE  2
#define AC_MAX_MATCHES 16
#define AC_MAX_VISIBLE 6
#define AC_POPUP_W     130
#define AC_ITEM_H      16
#define AC_PREFIX_MAX  15

static bool ac_active = false;
static int  ac_mode = AC_MODE_TAG;
static char ac_prefix[AC_PREFIX_MAX + 1];
static int  ac_prefix_len = 0;
static int  ac_sel = 0;
static int  ac_scroll = 0;
static int  ac_matches[AC_MAX_MATCHES];
static int  ac_match_count = 0;
static char ac_tag_ctx[16]; /* tag name for attribute context */

/* HTML tag database */
#define HTML_TAG_COUNT 17
static const char *html_tags[HTML_TAG_COUNT] = {
    "a", "b", "br", "div", "em", "h1", "h2", "h3", "hr",
    "i", "li", "ol", "p", "span", "strong", "u", "ul"
};
/* Void tags (self-closing, no </tag>) */
static bool html_void_tag(const char *tag) {
    return (str_eq(tag, "br") || str_eq(tag, "hr"));
}

/* Attribute database: tag -> attributes */
#define HTML_ATTR_COUNT 5
static const struct {
    const char *tag;  /* NULL = generic (applies to all) */
    const char *attr;
} html_attrs[HTML_ATTR_COUNT] = {
    { "a",    "href" },
    { "img",  "src"  },
    { "img",  "alt"  },
    { (const char *)0, "id"    },
    { (const char *)0, "class" },
};

static bool is_htm_file(void) {
    if (filename_len < 4) return false;
    /* Check .htm or .html ending */
    if (filename[filename_len - 4] == '.' &&
        (filename[filename_len - 3] == 'h' || filename[filename_len - 3] == 'H') &&
        (filename[filename_len - 2] == 't' || filename[filename_len - 2] == 'T') &&
        (filename[filename_len - 1] == 'm' || filename[filename_len - 1] == 'M'))
        return true;
    if (filename_len >= 5 &&
        filename[filename_len - 5] == '.' &&
        (filename[filename_len - 4] == 'h' || filename[filename_len - 4] == 'H') &&
        (filename[filename_len - 3] == 't' || filename[filename_len - 3] == 'T') &&
        (filename[filename_len - 2] == 'm' || filename[filename_len - 2] == 'M') &&
        (filename[filename_len - 1] == 'l' || filename[filename_len - 1] == 'L'))
        return true;
    return false;
}

/* Case-insensitive prefix match */
static bool ac_prefix_match(const char *candidate, const char *prefix, int plen) {
    for (int i = 0; i < plen; i++) {
        char a = candidate[i];
        char b = prefix[i];
        if (!a) return false;
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
    }
    return true;
}

static void ac_update_matches(void) {
    ac_match_count = 0;
    ac_sel = 0;
    ac_scroll = 0;

    if (ac_mode == AC_MODE_TAG || ac_mode == AC_MODE_CLOSE) {
        for (int i = 0; i < HTML_TAG_COUNT && ac_match_count < AC_MAX_MATCHES; i++) {
            if (ac_prefix_len == 0 || ac_prefix_match(html_tags[i], ac_prefix, ac_prefix_len))
                ac_matches[ac_match_count++] = i;
        }
    } else if (ac_mode == AC_MODE_ATTR) {
        for (int i = 0; i < HTML_ATTR_COUNT && ac_match_count < AC_MAX_MATCHES; i++) {
            /* Show attr if it's generic (tag==NULL) or matches current tag */
            if (html_attrs[i].tag == (const char *)0 || str_eq(html_attrs[i].tag, ac_tag_ctx)) {
                if (ac_prefix_len == 0 || ac_prefix_match(html_attrs[i].attr, ac_prefix, ac_prefix_len))
                    ac_matches[ac_match_count++] = i;
            }
        }
    }

    if (ac_match_count == 0) ac_active = false;
}

static void ac_start(int mode_val) {
    if (!is_htm_file()) return;
    ac_active = true;
    ac_mode = mode_val;
    ac_prefix_len = 0;
    ac_prefix[0] = '\0';
    ac_tag_ctx[0] = '\0';
    ac_update_matches();
}

/* Insert a string at cursor_pos */
static void insert_string(const char *s) {
    while (*s) {
        if (text_len >= BUF_SIZE) break;
        for (int i = text_len; i > cursor_pos; i--)
            text_buf[i] = text_buf[i - 1];
        text_buf[cursor_pos] = *s;
        text_len++;
        text_buf[text_len] = '\0';
        cursor_pos++;
        s++;
    }
    file_dirty = true;
}

/* Delete n chars before cursor */
static void delete_before(int n) {
    if (n > cursor_pos) n = cursor_pos;
    cursor_pos -= n;
    for (int i = cursor_pos; i < text_len - n; i++)
        text_buf[i] = text_buf[i + n];
    text_len -= n;
    text_buf[text_len] = '\0';
    file_dirty = true;
}

static void ac_accept(void) {
    if (!ac_active || ac_match_count == 0) return;

    /* Delete the prefix the user already typed */
    delete_before(ac_prefix_len);

    if (ac_mode == AC_MODE_TAG) {
        const char *tag = html_tags[ac_matches[ac_sel]];
        if (html_void_tag(tag)) {
            /* Void: insert tagname> */
            insert_string(tag);
            insert_string(">");
        } else {
            /* Container: insert tagname></tagname> then move cursor back */
            insert_string(tag);
            insert_string(">");
            int after_open = cursor_pos; /* save position after > */
            insert_string("</");
            insert_string(tag);
            insert_string(">");
            cursor_pos = after_open; /* position between >...</ tagname> */
        }
    } else if (ac_mode == AC_MODE_CLOSE) {
        const char *tag = html_tags[ac_matches[ac_sel]];
        insert_string(tag);
        insert_string(">");
    } else if (ac_mode == AC_MODE_ATTR) {
        const char *attr = html_attrs[ac_matches[ac_sel]].attr;
        insert_string(attr);
        insert_string("=\"\"");
        cursor_pos--; /* position between quotes */
    }

    ac_active = false;
}

/* Detect tag context: scan backwards from cursor to find the tag name
   after the last unmatched '<'. Returns the tag name in out, or empty. */
static void ac_find_tag_context(char *out, int max) {
    out[0] = '\0';
    int i = cursor_pos - 1;
    /* Skip back past the space we just typed */
    while (i >= 0 && text_buf[i] == ' ') i--;
    /* Now collect tag name chars backwards */
    int end = i + 1;
    while (i >= 0 && text_buf[i] != '<' && text_buf[i] != '>' && text_buf[i] != ' ') i--;
    if (i >= 0 && text_buf[i] == '<') {
        int start = i + 1;
        int len = end - start;
        if (len > max - 1) len = max - 1;
        for (int j = 0; j < len; j++) {
            char c = text_buf[start + j];
            if (c >= 'A' && c <= 'Z') c += 32;
            out[j] = c;
        }
        out[len] = '\0';
    }
}

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

/* Computed layout helpers */
static int get_text_x(void) {
    return TEXT_PAD + (show_line_numbers ? GUTTER_W : 0);
}

static int get_text_w(void) {
    return NP_W - get_text_x() - TEXT_PAD;
}

static int get_max_cols(void) {
    return get_text_w() / 8;
}

static int get_max_rows(void) {
    int text_h = NP_H - HEADER_H - STATUS_H - 4;
    return text_h / 16;
}

static const char *btn_labels_row1[ROW1_BTN_COUNT] = {
    "New", "Open", "Save", "Find", "Theme", "Color", "Ln#", "Wrap"
};

/* ---- Drawing helpers ---- */

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

/* ---- Theme access ---- */

static color_t theme_text_bg(void) {
    if (custom_colors) return custom_text_bg;
    return themes[current_theme].text_bg;
}

static color_t theme_text_fg(void) {
    if (custom_colors) return custom_text_fg;
    return themes[current_theme].text_fg;
}

static color_t theme_toolbar(void) {
    return themes[current_theme].toolbar_bg;
}

static color_t theme_status_bg(void) {
    return themes[current_theme].status_bg;
}

static color_t theme_status_fg(void) {
    return themes[current_theme].status_fg;
}

static color_t theme_btn_bg(void) {
    return themes[current_theme].btn_bg;
}

static color_t theme_btn_fg(void) {
    return themes[current_theme].btn_fg;
}

static color_t theme_cursor(void) {
    return themes[current_theme].cursor_color;
}

static color_t theme_gutter_bg(void) {
    return themes[current_theme].gutter_bg;
}

static color_t theme_gutter_fg(void) {
    return themes[current_theme].gutter_fg;
}

static color_t theme_highlight(void) {
    return themes[current_theme].highlight_bg;
}

/* ---- Cursor / text helpers ---- */

static void cursor_to_linecol(int pos, int *line, int *col) {
    int l = 0, c = 0;
    int max_c = word_wrap ? get_max_cols() : 9999;
    for (int i = 0; i < pos && i < text_len; i++) {
        if (text_buf[i] == '\n') { l++; c = 0; }
        else { c++; if (c >= max_c) { c = 0; l++; } }
    }
    *line = l;
    *col = c;
}

static int linecol_to_pos(int target_line, int target_col) {
    int l = 0, c = 0;
    int max_c = word_wrap ? get_max_cols() : 9999;
    for (int i = 0; i < text_len; i++) {
        if (l == target_line && c == target_col) return i;
        if (text_buf[i] == '\n') {
            if (l == target_line) return i;
            l++; c = 0;
        } else {
            c++;
            if (c >= max_c) { c = 0; l++; }
        }
    }
    return text_len;
}

/* Count the "real" line number (newline-delimited) at a buffer position */
static int real_line_at(int pos) {
    int l = 1;
    for (int i = 0; i < pos && i < text_len; i++)
        if (text_buf[i] == '\n') l++;
    return l;
}

static int count_words(void) {
    int words = 0;
    bool in_word = false;
    for (int i = 0; i < text_len; i++) {
        char c = text_buf[i];
        bool ws = (c == ' ' || c == '\n' || c == '\t');
        if (!ws && !in_word) { words++; in_word = true; }
        if (ws) in_word = false;
    }
    return words;
}

static int count_lines_real(void) {
    if (text_len == 0) return 1;
    int lines = 1;
    for (int i = 0; i < text_len; i++)
        if (text_buf[i] == '\n') lines++;
    return lines;
}

/* ---- int-to-string ---- */
static void int_to_str(int n, char *buf) {
    if (n == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    bool neg = false;
    if (n < 0) { neg = true; n = -n; }
    char tmp[12]; int i = 0;
    while (n > 0 && i < 11) { tmp[i++] = '0' + (n % 10); n /= 10; }
    int j = 0;
    if (neg) buf[j++] = '-';
    while (--i >= 0) buf[j++] = tmp[i];
    buf[j] = '\0';
}

/* ---- File operations ---- */

static void np_new_file(void) {
    text_len = 0;
    text_buf[0] = '\0';
    cursor_pos = 0;
    scroll_line = 0;
    filename[0] = '\0';
    filename_len = 0;
    file_dirty = false;
    find_match_pos = -1;
}

static void np_open_file(const char *name) {
    int idx = fs_find(name);
    if (idx < 0) return;
    struct fs_node *f = fs_get_node(idx);
    if (!f || f->type != FS_FILE || !f->data) return;

    int len = (int)f->size;
    if (len > BUF_SIZE) len = BUF_SIZE;
    mem_copy(text_buf, f->data, (uint32_t)len);
    text_len = len;
    text_buf[text_len] = '\0';
    cursor_pos = 0;
    scroll_line = 0;
    file_dirty = false;
    find_match_pos = -1;

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

    int i = 0;
    while (name[i] && i < 31) { filename[i] = name[i]; i++; }
    filename[i] = '\0';
    filename_len = i;
}

/* ---- Text editing ---- */

static void insert_char(char c) {
    if (text_len >= BUF_SIZE) return;
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
    if (line >= scroll_line + get_max_rows()) scroll_line = line - get_max_rows() + 1;
    if (scroll_line < 0) scroll_line = 0;
}

/* ---- Find/replace ---- */

static bool str_match_at(int pos) {
    if (find_len == 0) return false;
    for (int i = 0; i < find_len; i++) {
        if (pos + i >= text_len) return false;
        /* Case-insensitive comparison */
        char a = text_buf[pos + i];
        char b = find_buf[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
    }
    return true;
}

static void find_next(void) {
    if (find_len == 0) { find_match_pos = -1; return; }
    int start = (find_match_pos >= 0) ? find_match_pos + 1 : 0;
    /* Search from start to end */
    for (int i = start; i <= text_len - find_len; i++) {
        if (str_match_at(i)) { find_match_pos = i; cursor_pos = i; ensure_cursor_visible(); return; }
    }
    /* Wrap around */
    for (int i = 0; i < start && i <= text_len - find_len; i++) {
        if (str_match_at(i)) { find_match_pos = i; cursor_pos = i; ensure_cursor_visible(); return; }
    }
    find_match_pos = -1;
}

static void replace_current(void) {
    if (find_match_pos < 0 || find_len == 0) return;
    /* Delete find_len chars at find_match_pos */
    int diff = replace_len - find_len;
    if (text_len + diff > BUF_SIZE) return;

    if (diff > 0) {
        /* Shift right */
        for (int i = text_len - 1; i >= find_match_pos + find_len; i--)
            text_buf[i + diff] = text_buf[i];
    } else if (diff < 0) {
        /* Shift left */
        for (int i = find_match_pos + find_len; i < text_len; i++)
            text_buf[i + diff] = text_buf[i];
    }
    /* Copy replacement */
    for (int i = 0; i < replace_len; i++)
        text_buf[find_match_pos + i] = replace_buf[i];

    text_len += diff;
    text_buf[text_len] = '\0';
    file_dirty = true;
    cursor_pos = find_match_pos + replace_len;
    find_match_pos = -1;
    find_next(); /* Find the next occurrence */
}

/* ---- Event handler ---- */

static void np_on_event(struct window *win, struct gui_event *evt) {
    (void)win;

    if (evt->type == EVT_KEY_PRESS) {
        uint8_t k = evt->key;

        /* Ctrl+F → find */
        if (k == 6) {
            mode = MODE_FIND;
            find_field_active = true;
            return;
        }
        /* Ctrl+S → quick save */
        if (k == 19) {
            if (filename_len > 0) np_save_file(filename);
            else { input_len = 0; mode = MODE_SAVE; }
            return;
        }
        /* Ctrl+N → new file */
        if (k == 14) {
            np_new_file();
            mode = MODE_EDIT;
            return;
        }

        /* File picker (Open) */
        if (mode == MODE_OPEN) {
            if (k == 0x1B) { mode = MODE_EDIT; return; }
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
                if (f) np_open_file(f->name);
                mode = MODE_EDIT;
                return;
            }
            return;
        }

        /* Save dialog input */
        if (mode == MODE_SAVE) {
            if (k == 0x1B) { mode = MODE_EDIT; return; }
            if (k == '\n') {
                if (input_len > 0) {
                    input_buf[input_len] = '\0';
                    np_save_file(input_buf);
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

        /* Find/replace mode */
        if (mode == MODE_FIND) {
            if (k == 0x1B) { mode = MODE_EDIT; return; }
            if (k == '\t') { find_field_active = !find_field_active; return; }
            if (k == '\n') {
                if (find_field_active) {
                    find_buf[find_len] = '\0';
                    find_next();
                } else {
                    replace_buf[replace_len] = '\0';
                    replace_current();
                }
                return;
            }
            if (k == '\b') {
                if (find_field_active) { if (find_len > 0) find_len--; find_match_pos = -1; }
                else { if (replace_len > 0) replace_len--; }
                return;
            }
            if (k >= 0x20 && k < 0x80) {
                if (find_field_active && find_len < FIND_MAX) {
                    find_buf[find_len++] = (char)k;
                    find_match_pos = -1;
                } else if (!find_field_active && replace_len < REPLACE_MAX) {
                    replace_buf[replace_len++] = (char)k;
                }
            }
            return;
        }

        /* Color picker mode - Esc to exit */
        if (mode == MODE_COLOR) {
            if (k == 0x1B) { mode = MODE_EDIT; return; }
            return;
        }

        /* Normal edit mode */

        /* --- Autocomplete active: intercept keys --- */
        if (ac_active) {
            if (k == '\t' || k == '\n') {
                ac_accept();
                ensure_cursor_visible();
                return;
            }
            if (k == 0x1B) { ac_active = false; return; }
            if (k == KEY_UP) {
                if (ac_sel > 0) ac_sel--;
                if (ac_sel < ac_scroll) ac_scroll = ac_sel;
                return;
            }
            if (k == KEY_DOWN) {
                if (ac_sel < ac_match_count - 1) ac_sel++;
                if (ac_sel >= ac_scroll + AC_MAX_VISIBLE)
                    ac_scroll = ac_sel - AC_MAX_VISIBLE + 1;
                return;
            }
            if (k == '\b') {
                if (ac_prefix_len > 0) {
                    ac_prefix_len--;
                    ac_prefix[ac_prefix_len] = '\0';
                    delete_char_back();
                    ac_update_matches();
                    ensure_cursor_visible();
                } else {
                    ac_active = false;
                    delete_char_back();
                    ensure_cursor_visible();
                }
                return;
            }
            if (k >= 0x20 && k < 0x80 && k != '<' && k != '>') {
                if (ac_prefix_len < AC_PREFIX_MAX) {
                    ac_prefix[ac_prefix_len++] = (char)k;
                    ac_prefix[ac_prefix_len] = '\0';
                    insert_char((char)k);
                    ac_update_matches();
                    ensure_cursor_visible();
                } else {
                    ac_active = false;
                    insert_char((char)k);
                    ensure_cursor_visible();
                }
                return;
            }
            /* For '>' or other special: dismiss and fall through */
            ac_active = false;
        }

        if (k == '\b') { delete_char_back(); ensure_cursor_visible(); return; }
        if (k == '\n') { insert_char('\n'); ensure_cursor_visible(); return; }

        if (k == KEY_LEFT) {
            if (cursor_pos > 0) cursor_pos--;
            ensure_cursor_visible(); return;
        }
        if (k == KEY_RIGHT) {
            if (cursor_pos < text_len) cursor_pos++;
            ensure_cursor_visible(); return;
        }
        if (k == KEY_UP) {
            int line, col;
            cursor_to_linecol(cursor_pos, &line, &col);
            if (line > 0) cursor_pos = linecol_to_pos(line - 1, col);
            ensure_cursor_visible(); return;
        }
        if (k == KEY_DOWN) {
            int line, col;
            cursor_to_linecol(cursor_pos, &line, &col);
            cursor_pos = linecol_to_pos(line + 1, col);
            if (cursor_pos > text_len) cursor_pos = text_len;
            ensure_cursor_visible(); return;
        }
        if (k == KEY_HOME) {
            while (cursor_pos > 0 && text_buf[cursor_pos - 1] != '\n')
                cursor_pos--;
            ensure_cursor_visible(); return;
        }
        if (k == KEY_END) {
            while (cursor_pos < text_len && text_buf[cursor_pos] != '\n')
                cursor_pos++;
            ensure_cursor_visible(); return;
        }
        if (k == KEY_PGUP) {
            int rows = get_max_rows();
            int line, col;
            cursor_to_linecol(cursor_pos, &line, &col);
            if (line > rows) cursor_pos = linecol_to_pos(line - rows, col);
            else cursor_pos = linecol_to_pos(0, col);
            ensure_cursor_visible(); return;
        }
        if (k == KEY_PGDN) {
            int rows = get_max_rows();
            int line, col;
            cursor_to_linecol(cursor_pos, &line, &col);
            cursor_pos = linecol_to_pos(line + rows, col);
            if (cursor_pos > text_len) cursor_pos = text_len;
            ensure_cursor_visible(); return;
        }
        if (k == KEY_DELETE) {
            if (cursor_pos < text_len) {
                for (int i = cursor_pos; i < text_len - 1; i++)
                    text_buf[i] = text_buf[i + 1];
                text_len--;
                text_buf[text_len] = '\0';
                file_dirty = true;
            }
            return;
        }

        /* Printable characters */
        if (k >= 0x20 && k < 0x80) {
            insert_char((char)k);
            ensure_cursor_visible();

            /* Autocomplete triggers for HTML files */
            if (is_htm_file()) {
                if ((char)k == '<') {
                    ac_start(AC_MODE_TAG);
                } else if ((char)k == '/' && cursor_pos >= 2 && text_buf[cursor_pos - 2] == '<') {
                    /* Typed </ — closing tag mode */
                    ac_start(AC_MODE_CLOSE);
                } else if ((char)k == ' ') {
                    /* Space inside a tag? Check if we're between < and unclosed > */
                    char ctx[16];
                    ac_find_tag_context(ctx, 16);
                    if (ctx[0]) {
                        ac_mode = AC_MODE_ATTR;
                        ac_active = true;
                        ac_prefix_len = 0;
                        ac_prefix[0] = '\0';
                        str_ncopy(ac_tag_ctx, ctx, 15);
                        ac_update_matches();
                    }
                }
            }
        }
        return;
    }

    if (evt->type == EVT_MOUSE_DOWN) {
        int mx = evt->mouse_x - (win->x + BORDER_WIDTH);
        int my = evt->mouse_y - (win->y + TITLEBAR_HEIGHT);

        /* File picker click handling */
        if (mode == MODE_OPEN && picker_count > 0) {
            int popup_w = 260;
            int popup_h = 20 + PICKER_VISIBLE * 18 + 4;
            int popup_x = (NP_W - popup_w) / 2;
            int popup_y = (NP_H - popup_h) / 2;
            int list_x = popup_x + 4;
            int list_y = popup_y + 20;
            if (mx >= list_x && mx < list_x + popup_w - 8 &&
                my >= list_y && my < list_y + PICKER_VISIBLE * 18) {
                int idx = (my - list_y) / 18 + picker_scroll;
                if (idx >= 0 && idx < picker_count) {
                    struct fs_node *f = fs_get_node(picker_entries[idx]);
                    if (f) np_open_file(f->name);
                    mode = MODE_EDIT;
                }
                return;
            }
        }

        /* Row 1 toolbar buttons */
        if (my >= 3 && my < 3 + BTN_H) {
            for (int i = 0; i < ROW1_BTN_COUNT; i++) {
                int bx = 4 + i * (BTN_W + BTN_GAP);
                if (mx >= bx && mx < bx + BTN_W) {
                    switch (i) {
                    case 0: /* New */
                        np_new_file(); mode = MODE_EDIT; break;
                    case 1: /* Open */
                        picker_refresh(); mode = MODE_OPEN; break;
                    case 2: /* Save */
                        if (filename_len > 0) np_save_file(filename);
                        else { input_len = 0; mode = MODE_SAVE; }
                        break;
                    case 3: /* Find */
                        mode = MODE_FIND; find_field_active = true; break;
                    case 4: /* Theme */
                        current_theme = (current_theme + 1) % THEME_COUNT;
                        custom_colors = false;
                        break;
                    case 5: /* Color */
                        mode = (mode == MODE_COLOR) ? MODE_EDIT : MODE_COLOR;
                        break;
                    case 6: /* Ln# */
                        show_line_numbers = !show_line_numbers; break;
                    case 7: /* Wrap */
                        word_wrap = !word_wrap; break;
                    }
                    return;
                }
            }
        }

        /* Row 2 (theme info bar) - nothing clickable, but check if in color picker */
        if (mode == MODE_COLOR) {
            /* Color picker area */
            int cp_y = HEADER_H + 2;
            int cp_x = TEXT_PAD;
            int swatch = 20;
            int gap = 2;

            /* "Text:" label area */
            int label_h = 18;
            int fg_grid_y = cp_y + label_h;
            int bg_label_y = fg_grid_y + PALETTE_ROWS * (swatch + gap) + 8;
            int bg_grid_y = bg_label_y + label_h;

            /* Check FG palette click */
            if (my >= fg_grid_y && my < fg_grid_y + PALETTE_ROWS * (swatch + gap)) {
                int row = (my - fg_grid_y) / (swatch + gap);
                int col = (mx - cp_x) / (swatch + gap);
                if (col >= 0 && col < PALETTE_COLS && row >= 0 && row < PALETTE_ROWS) {
                    int idx = row * PALETTE_COLS + col;
                    if (idx < PALETTE_COUNT) {
                        custom_text_fg = palette[idx];
                        custom_colors = true;
                    }
                }
                return;
            }

            /* Check BG palette click */
            if (my >= bg_grid_y && my < bg_grid_y + PALETTE_ROWS * (swatch + gap)) {
                int row = (my - bg_grid_y) / (swatch + gap);
                int col = (mx - cp_x) / (swatch + gap);
                if (col >= 0 && col < PALETTE_COLS && row >= 0 && row < PALETTE_ROWS) {
                    int idx = row * PALETTE_COLS + col;
                    if (idx < PALETTE_COUNT) {
                        custom_text_bg = palette[idx];
                        custom_colors = true;
                    }
                }
                return;
            }
            return;
        }

        /* Text area click - position cursor */
        int text_x = get_text_x();
        int text_y_start = HEADER_H + 2;
        int text_h = NP_H - HEADER_H - STATUS_H - 4;
        if (mx >= text_x && mx < text_x + get_text_w() &&
            my >= text_y_start && my < text_y_start + text_h) {
            int click_col = (mx - text_x) / 8;
            int click_row = (my - text_y_start) / 16 + scroll_line;
            int pos = linecol_to_pos(click_row, click_col);
            if (pos <= text_len) cursor_pos = pos;
        }
    }
}

void notepad_create(void) {
    if (win_id >= 0) {
        struct window *w = wm_get_window(win_id);
        if (w && w->alive && w->on_event == np_on_event) { wm_focus_window(win_id); return; }
    }
    win_id = wm_create_window("Notepad", 80, 50, NP_W, NP_H,
                               np_on_event, NULL);
    np_new_file();
    mode = MODE_EDIT;
}

bool notepad_is_alive(void) {
    if (win_id < 0) return false;
    struct window *w = wm_get_window(win_id);
    return (w && w->alive);
}

void notepad_render(void) {
    if (win_id < 0) return;
    struct window *win = wm_get_window(win_id);
    if (!win || !win->alive || !win->content || win->on_event != np_on_event) { win_id = -1; return; }

    int cw = win->content_w;
    int ch = win->content_h;
    color_t *buf = win->content;

    /* Background */
    color_t bg = theme_text_bg();
    for (int i = 0; i < cw * ch; i++) buf[i] = bg;

    /* Toolbar row 1 */
    np_rect(buf, cw, ch, 0, 0, cw, TOOLBAR_H, theme_toolbar());
    for (int i = 0; i < ROW1_BTN_COUNT; i++) {
        int bx = 4 + i * (BTN_W + BTN_GAP);
        color_t btn_c = theme_btn_bg();
        /* Highlight active toggle buttons */
        if (i == 6 && show_line_numbers) btn_c = COLOR_RGB(40, 120, 60);
        if (i == 7 && word_wrap) btn_c = COLOR_RGB(40, 120, 60);
        np_rect(buf, cw, ch, bx, 3, BTN_W, BTN_H, btn_c);
        int llen = str_len(btn_labels_row1[i]);
        int lx = bx + (BTN_W - llen * 8) / 2;
        np_text(buf, cw, ch, lx, 4, btn_labels_row1[i], theme_btn_fg(), btn_c);
    }

    /* Toolbar row 2 - info bar */
    np_rect(buf, cw, ch, 0, TOOLBAR2_Y, cw, TOOLBAR2_H, theme_toolbar());

    /* Show filename and theme name */
    int ix = 4;
    if (filename_len > 0) {
        np_text(buf, cw, ch, ix, TOOLBAR2_Y + 3, filename, COLOR_YELLOW, theme_toolbar());
        ix += filename_len * 8;
        if (file_dirty) {
            np_text(buf, cw, ch, ix, TOOLBAR2_Y + 3, " *", COLOR_RED, theme_toolbar());
            ix += 16;
        }
        ix += 16;
    } else {
        np_text(buf, cw, ch, ix, TOOLBAR2_Y + 3, "untitled", COLOR_RGB(120,120,120), theme_toolbar());
        ix += 8 * 8 + 16;
    }

    /* Theme indicator */
    np_text(buf, cw, ch, ix, TOOLBAR2_Y + 3, "[", theme_status_fg(), theme_toolbar());
    ix += 8;
    const char *tname = custom_colors ? "Custom" : themes[current_theme].name;
    np_text(buf, cw, ch, ix, TOOLBAR2_Y + 3, tname, theme_cursor(), theme_toolbar());
    ix += str_len(tname) * 8;
    np_text(buf, cw, ch, ix, TOOLBAR2_Y + 3, "]", theme_status_fg(), theme_toolbar());

    /* Color picker panel */
    if (mode == MODE_COLOR) {
        int cp_y = HEADER_H + 2;
        int cp_x = TEXT_PAD;
        int swatch = 20;
        int gap = 2;

        /* Background for color picker area */
        np_rect(buf, cw, ch, 0, HEADER_H, cw, NP_H - HEADER_H - STATUS_H, theme_toolbar());

        np_text(buf, cw, ch, cp_x, cp_y, "Text Color:", theme_text_fg(), theme_toolbar());
        int fg_grid_y = cp_y + 18;
        for (int r = 0; r < PALETTE_ROWS; r++) {
            for (int c = 0; c < PALETTE_COLS; c++) {
                int idx = r * PALETTE_COLS + c;
                int sx = cp_x + c * (swatch + gap);
                int sy = fg_grid_y + r * (swatch + gap);
                np_rect(buf, cw, ch, sx, sy, swatch, swatch, palette[idx]);
                /* Highlight if selected */
                if (custom_colors && palette[idx] == custom_text_fg) {
                    np_rect(buf, cw, ch, sx, sy, swatch, 1, COLOR_WHITE);
                    np_rect(buf, cw, ch, sx, sy + swatch - 1, swatch, 1, COLOR_WHITE);
                    np_rect(buf, cw, ch, sx, sy, 1, swatch, COLOR_WHITE);
                    np_rect(buf, cw, ch, sx + swatch - 1, sy, 1, swatch, COLOR_WHITE);
                }
            }
        }

        int bg_label_y = fg_grid_y + PALETTE_ROWS * (swatch + gap) + 8;
        np_text(buf, cw, ch, cp_x, bg_label_y, "Background:", theme_text_fg(), theme_toolbar());
        int bg_grid_y = bg_label_y + 18;
        for (int r = 0; r < PALETTE_ROWS; r++) {
            for (int c = 0; c < PALETTE_COLS; c++) {
                int idx = r * PALETTE_COLS + c;
                int sx = cp_x + c * (swatch + gap);
                int sy = bg_grid_y + r * (swatch + gap);
                np_rect(buf, cw, ch, sx, sy, swatch, swatch, palette[idx]);
                if (custom_colors && palette[idx] == custom_text_bg) {
                    np_rect(buf, cw, ch, sx, sy, swatch, 1, COLOR_WHITE);
                    np_rect(buf, cw, ch, sx, sy + swatch - 1, swatch, 1, COLOR_WHITE);
                    np_rect(buf, cw, ch, sx, sy, 1, swatch, COLOR_WHITE);
                    np_rect(buf, cw, ch, sx + swatch - 1, sy, 1, swatch, COLOR_WHITE);
                }
            }
        }

        /* Preview area */
        int pv_x = cp_x + PALETTE_COLS * (swatch + gap) + 16;
        int pv_y = cp_y + 18;
        np_text(buf, cw, ch, pv_x, cp_y, "Preview:", theme_status_fg(), theme_toolbar());
        np_rect(buf, cw, ch, pv_x, pv_y, 140, 48, theme_text_bg());
        np_text(buf, cw, ch, pv_x + 4, pv_y + 4, "Hello World", theme_text_fg(), theme_text_bg());
        np_text(buf, cw, ch, pv_x + 4, pv_y + 20, "Sample text", theme_text_fg(), theme_text_bg());

        np_text(buf, cw, ch, pv_x, pv_y + 60, "Esc to close", theme_status_fg(), theme_toolbar());

    } else {
        /* Normal text area */
        int text_x = get_text_x();
        int text_y = HEADER_H + 2;
        int text_area_h = NP_H - HEADER_H - STATUS_H - 4;
        int max_rows = get_max_rows();
        int max_cols = get_max_cols();

        /* Text area background */
        np_rect(buf, cw, ch, 0, HEADER_H, cw, text_area_h + 4, theme_text_bg());

        /* Line number gutter */
        if (show_line_numbers) {
            np_rect(buf, cw, ch, 0, HEADER_H, GUTTER_W, text_area_h + 4, theme_gutter_bg());
            /* Separator line */
            np_rect(buf, cw, ch, GUTTER_W - 1, HEADER_H, 1, text_area_h + 4,
                    theme_gutter_fg());
        }

        /* Render text with optional line numbers */
        int line = 0, col = 0;
        int cur_line = -1, cur_col = -1;
        cursor_to_linecol(cursor_pos, &cur_line, &cur_col);

        /* Track real line numbers for the gutter */
        int real_ln = 1;
        int last_drawn_real_ln = -1;

        for (int i = 0; i <= text_len; i++) {
            /* Draw line number at start of each visible screen line */
            if (show_line_numbers && col == 0 && line >= scroll_line && line < scroll_line + max_rows) {
                int screen_row = line - scroll_line;
                /* Only draw the line number once per real line */
                if (real_ln != last_drawn_real_ln) {
                    char lnbuf[6];
                    int_to_str(real_ln, lnbuf);
                    int lnlen = str_len(lnbuf);
                    /* Right-align in gutter */
                    int gx = GUTTER_W - 6 - lnlen * 8;
                    if (gx < 2) gx = 2;
                    np_text(buf, cw, ch, gx, text_y + screen_row * 16,
                            lnbuf, theme_gutter_fg(), theme_gutter_bg());
                    last_drawn_real_ln = real_ln;
                }
            }

            if (i == text_len) break;

            /* Draw character if visible */
            if (line >= scroll_line && line < scroll_line + max_rows) {
                int screen_row = line - scroll_line;
                int px = text_x + col * 8;
                int py = text_y + screen_row * 16;

                /* Check if this position is part of a find match */
                bool in_match = false;
                if (find_match_pos >= 0 && find_len > 0 &&
                    i >= find_match_pos && i < find_match_pos + find_len) {
                    in_match = true;
                }

                if (text_buf[i] != '\n') {
                    if (in_match) {
                        /* Highlight match */
                        np_rect(buf, cw, ch, px, py, 8, 16, theme_highlight());
                        np_char_transparent(buf, cw, ch, px, py, text_buf[i], theme_text_fg());
                    } else {
                        np_char_transparent(buf, cw, ch, px, py, text_buf[i], theme_text_fg());
                    }
                }
            }

            if (text_buf[i] == '\n') {
                line++; col = 0; real_ln++;
            } else {
                col++;
                if (word_wrap && col >= max_cols) { col = 0; line++; }
            }
        }

        /* Blinking cursor */
        if (mode == MODE_EDIT && (timer_get_ticks() / 40) & 1) {
            if (cur_line >= scroll_line && cur_line < scroll_line + max_rows) {
                int screen_row = cur_line - scroll_line;
                int cx = text_x + cur_col * 8;
                int cy = text_y + screen_row * 16;
                np_rect(buf, cw, ch, cx, cy, 2, 16, theme_cursor());
            }
        }

        /* Autocomplete popup */
        if (ac_active && ac_match_count > 0 && mode == MODE_EDIT) {
            int screen_row = cur_line - scroll_line;
            int popup_x = text_x + cur_col * 8;
            int popup_y = text_y + (screen_row + 1) * 16 + 2;

            /* If popup would go below content area, show above cursor */
            int visible = ac_match_count;
            if (visible > AC_MAX_VISIBLE) visible = AC_MAX_VISIBLE;
            int popup_h = visible * AC_ITEM_H + 4;
            if (popup_y + popup_h > NP_H - STATUS_H) {
                popup_y = text_y + screen_row * 16 - popup_h - 2;
                if (popup_y < HEADER_H) popup_y = HEADER_H;
            }
            /* Clamp horizontal */
            if (popup_x + AC_POPUP_W > NP_W - 4) popup_x = NP_W - 4 - AC_POPUP_W;
            if (popup_x < 4) popup_x = 4;

            /* Background + border */
            np_rect(buf, cw, ch, popup_x, popup_y, AC_POPUP_W, popup_h, COLOR_RGB(35, 35, 48));
            np_rect(buf, cw, ch, popup_x, popup_y, AC_POPUP_W, 1, COLOR_RGB(80, 120, 200));
            np_rect(buf, cw, ch, popup_x, popup_y + popup_h - 1, AC_POPUP_W, 1, COLOR_RGB(80, 120, 200));
            np_rect(buf, cw, ch, popup_x, popup_y, 1, popup_h, COLOR_RGB(80, 120, 200));
            np_rect(buf, cw, ch, popup_x + AC_POPUP_W - 1, popup_y, 1, popup_h, COLOR_RGB(80, 120, 200));

            for (int i = 0; i < visible; i++) {
                int idx = ac_scroll + i;
                if (idx >= ac_match_count) break;
                int iy = popup_y + 2 + i * AC_ITEM_H;
                color_t row_bg = (idx == ac_sel) ? COLOR_RGB(50, 70, 140) : COLOR_RGB(35, 35, 48);
                np_rect(buf, cw, ch, popup_x + 2, iy, AC_POPUP_W - 4, AC_ITEM_H, row_bg);

                const char *label;
                if (ac_mode == AC_MODE_ATTR)
                    label = html_attrs[ac_matches[idx]].attr;
                else
                    label = html_tags[ac_matches[idx]];

                /* Tag icon/prefix */
                if (ac_mode == AC_MODE_TAG) {
                    np_text(buf, cw, ch, popup_x + 6, iy, "<", COLOR_RGB(100, 160, 220), row_bg);
                    np_text(buf, cw, ch, popup_x + 14, iy, label, COLOR_WHITE, row_bg);
                    np_text(buf, cw, ch, popup_x + 14 + str_len(label) * 8, iy, ">",
                            COLOR_RGB(100, 160, 220), row_bg);
                } else if (ac_mode == AC_MODE_CLOSE) {
                    np_text(buf, cw, ch, popup_x + 6, iy, "/", COLOR_RGB(100, 160, 220), row_bg);
                    np_text(buf, cw, ch, popup_x + 14, iy, label, COLOR_WHITE, row_bg);
                    np_text(buf, cw, ch, popup_x + 14 + str_len(label) * 8, iy, ">",
                            COLOR_RGB(100, 160, 220), row_bg);
                } else {
                    np_text(buf, cw, ch, popup_x + 6, iy, label, COLOR_RGB(180, 220, 255), row_bg);
                    np_text(buf, cw, ch, popup_x + 6 + str_len(label) * 8 + 4, iy, "=\"\"",
                            COLOR_RGB(100, 100, 120), row_bg);
                }
            }

            /* Scroll indicators */
            if (ac_scroll > 0)
                np_text(buf, cw, ch, popup_x + AC_POPUP_W - 12, popup_y + 2, "^",
                        COLOR_RGB(120, 120, 140), COLOR_RGB(35, 35, 48));
            if (ac_scroll + visible < ac_match_count)
                np_text(buf, cw, ch, popup_x + AC_POPUP_W - 12, popup_y + popup_h - AC_ITEM_H - 2, "v",
                        COLOR_RGB(120, 120, 140), COLOR_RGB(35, 35, 48));
        }
    }

    /* Status bar */
    np_rect(buf, cw, ch, 0, NP_H - STATUS_H, NP_W, STATUS_H, theme_status_bg());

    if (mode == MODE_OPEN) {
        /* File picker popup overlay */
        int popup_w = 260;
        int popup_h = 20 + PICKER_VISIBLE * 18 + 4;
        int popup_x = (NP_W - popup_w) / 2;
        int popup_y = (NP_H - popup_h) / 2;

        /* Background */
        np_rect(buf, cw, ch, popup_x, popup_y, popup_w, popup_h, COLOR_RGB(30, 30, 40));
        /* Border */
        np_rect(buf, cw, ch, popup_x, popup_y, popup_w, 1, COLOR_RGB(100, 100, 120));
        np_rect(buf, cw, ch, popup_x, popup_y + popup_h - 1, popup_w, 1, COLOR_RGB(100, 100, 120));
        np_rect(buf, cw, ch, popup_x, popup_y, 1, popup_h, COLOR_RGB(100, 100, 120));
        np_rect(buf, cw, ch, popup_x + popup_w - 1, popup_y, 1, popup_h, COLOR_RGB(100, 100, 120));
        /* Title */
        np_text(buf, cw, ch, popup_x + 8, popup_y + 2, "Open File", COLOR_YELLOW, COLOR_RGB(30, 30, 40));

        if (picker_count == 0) {
            np_text(buf, cw, ch, popup_x + 8, popup_y + 22, "(no files)", COLOR_RGB(120, 120, 120), COLOR_RGB(30, 30, 40));
        } else {
            int list_y = popup_y + 20;
            int visible = picker_count - picker_scroll;
            if (visible > PICKER_VISIBLE) visible = PICKER_VISIBLE;
            for (int i = 0; i < visible; i++) {
                int fi = picker_scroll + i;
                int iy = list_y + i * 18;
                struct fs_node *f = fs_get_node(picker_entries[fi]);
                if (!f) continue;
                color_t row_bg = (fi == picker_sel) ? COLOR_RGB(50, 80, 130) : COLOR_RGB(30, 30, 40);
                np_rect(buf, cw, ch, popup_x + 2, iy, popup_w - 4, 18, row_bg);
                np_text(buf, cw, ch, popup_x + 8, iy + 1, f->name, COLOR_WHITE, row_bg);
            }
        }
        /* Hint */
        np_text(buf, cw, ch, popup_x + 8, popup_y + popup_h - 18,
                "Enter=Open  Esc=Cancel", COLOR_RGB(100, 100, 100), COLOR_RGB(30, 30, 40));
    } else if (mode == MODE_SAVE) {
        const char *prompt = "Save as: ";
        np_text(buf, cw, ch, 4, NP_H - STATUS_H + 2, prompt, COLOR_YELLOW, theme_status_bg());
        input_buf[input_len] = '\0';
        int px = 4 + str_len(prompt) * 8;
        np_text(buf, cw, ch, px, NP_H - STATUS_H + 2, input_buf, COLOR_WHITE, theme_status_bg());
        if ((timer_get_ticks() / 40) & 1)
            np_rect(buf, cw, ch, px + input_len * 8, NP_H - STATUS_H + 2, 8, 16, theme_cursor());
    } else if (mode == MODE_FIND) {
        /* Find/replace bar */
        int fy = NP_H - STATUS_H + 2;
        find_buf[find_len] = '\0';
        replace_buf[replace_len] = '\0';

        /* Find field */
        color_t find_label_c = find_field_active ? COLOR_YELLOW : theme_status_fg();
        np_text(buf, cw, ch, 4, fy, "F:", find_label_c, theme_status_bg());
        np_text(buf, cw, ch, 24, fy, find_buf, COLOR_WHITE, theme_status_bg());
        if (find_field_active && (timer_get_ticks() / 40) & 1)
            np_rect(buf, cw, ch, 24 + find_len * 8, fy, 8, 16, theme_cursor());

        /* Replace field */
        int rx = 24 + (FIND_MAX > 16 ? 16 : FIND_MAX) * 8 + 16;
        if (rx > 200) rx = 200;
        color_t rep_label_c = !find_field_active ? COLOR_YELLOW : theme_status_fg();
        np_text(buf, cw, ch, rx, fy, "R:", rep_label_c, theme_status_bg());
        np_text(buf, cw, ch, rx + 20, fy, replace_buf, COLOR_WHITE, theme_status_bg());
        if (!find_field_active && (timer_get_ticks() / 40) & 1)
            np_rect(buf, cw, ch, rx + 20 + replace_len * 8, fy, 8, 16, theme_cursor());

        /* Tab hint */
        np_text(buf, cw, ch, NP_W - 80, fy, "Tab=swap", COLOR_RGB(100,100,100), theme_status_bg());
    } else {
        /* Normal status: Ln X Col Y | W words Z chars */
        int cur_line, cur_col;
        cursor_to_linecol(cursor_pos, &cur_line, &cur_col);

        char status[64];
        int si = 0;
        char nbuf[12];

        /* Ln */
        status[si++] = 'L'; status[si++] = 'n'; status[si++] = ' ';
        int_to_str(real_line_at(cursor_pos), nbuf);
        for (int i = 0; nbuf[i]; i++) status[si++] = nbuf[i];

        /* Col */
        status[si++] = ' '; status[si++] = 'C'; status[si++] = 'o';
        status[si++] = 'l'; status[si++] = ' ';
        int_to_str(cur_col + 1, nbuf);
        for (int i = 0; nbuf[i]; i++) status[si++] = nbuf[i];

        status[si++] = ' '; status[si++] = '|'; status[si++] = ' ';

        /* Words */
        int_to_str(count_words(), nbuf);
        for (int i = 0; nbuf[i]; i++) status[si++] = nbuf[i];
        status[si++] = ' '; status[si++] = 'w'; status[si++] = 'o';
        status[si++] = 'r'; status[si++] = 'd'; status[si++] = 's';

        status[si++] = ' ';

        /* Lines */
        int_to_str(count_lines_real(), nbuf);
        for (int i = 0; nbuf[i]; i++) status[si++] = nbuf[i];
        status[si++] = ' '; status[si++] = 'l'; status[si++] = 'n';
        status[si++] = 's';

        status[si++] = ' ';

        /* Chars */
        int_to_str(text_len, nbuf);
        for (int i = 0; nbuf[i]; i++) status[si++] = nbuf[i];
        status[si++] = ' '; status[si++] = 'c'; status[si++] = 'h';
        status[si] = '\0';

        np_text(buf, cw, ch, 4, NP_H - STATUS_H + 2, status, theme_status_fg(), theme_status_bg());
    }
}
