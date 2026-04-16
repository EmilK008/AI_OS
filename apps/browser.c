/* ===========================================================================
 * Browser - Simple HTML file viewer for local filesystem
 * Renders basic HTML tags: h1-h3, p, b, i, u, br, hr, a, ul/li
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
#define STATUS_H    18
#define CONTENT_Y   TOOLBAR_H
#define CONTENT_H   (BRW_H - TOOLBAR_H - STATUS_H)

/* Address bar */
#define ADDR_X      40
#define ADDR_W      (BRW_W - ADDR_X - 40)
#define ADDR_MAX    30

/* Colors */
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
#define C_LINK_HOVER COLOR_RGB(0, 120, 255)
#define C_HR         COLOR_RGB(180, 180, 180)
#define C_BULLET     COLOR_RGB(80, 80, 80)
#define C_BTN_BG    COLOR_RGB(200, 200, 208)
#define C_BTN_FG    COLOR_RGB(40, 40, 40)

/* Page content buffer */
#define PAGE_BUF_SIZE  8192
static char page_buf[PAGE_BUF_SIZE];
static int  page_len = 0;

/* Address bar state */
static char addr_buf[ADDR_MAX + 1];
static int  addr_len = 0;
static bool addr_focused = false;

/* Navigation history */
#define HISTORY_MAX 16
static char history[HISTORY_MAX][ADDR_MAX + 1];
static int  history_count = 0;

/* Scroll */
static int scroll_y = 0;
static int content_total_h = 0;  /* total rendered height */

/* Clickable link regions */
#define MAX_LINKS 32
struct link_region {
    int x, y, w, h;
    char href[ADDR_MAX + 1];
};
static struct link_region links[MAX_LINKS];
static int link_count = 0;
static int hovered_link = -1;

/* Window */
static int win_id = -1;

/* Status message */
static char status_msg[64];

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

/* Draw underline under a character position */
static void brw_underline(color_t *buf, int cw, int ch, int px, int py, int char_w, color_t c) {
    int uy = py + 15;  /* bottom of 16px glyph */
    if (uy >= 0 && uy < ch) {
        for (int x = px; x < px + char_w && x < cw; x++) {
            if (x >= 0) buf[uy * cw + x] = c;
        }
    }
}

/* ---- HTML tag parsing helpers ---- */

/* Case-insensitive compare for tag names */
static bool tag_eq(const char *tag, int tlen, const char *name) {
    int nlen = str_len(name);
    if (tlen != nlen) return false;
    for (int i = 0; i < tlen; i++) {
        char a = tag[i];
        char b = name[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return false;
    }
    return true;
}

/* Extract href="..." from an <a> tag body like: a href="file.htm" */
static void extract_href(const char *tag_body, int body_len, char *href_out, int max) {
    href_out[0] = '\0';
    /* Find href=" */
    for (int i = 0; i < body_len - 5; i++) {
        char c0 = tag_body[i]; if (c0 >= 'A' && c0 <= 'Z') c0 += 32;
        char c1 = tag_body[i+1]; if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        char c2 = tag_body[i+2]; if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        char c3 = tag_body[i+3]; if (c3 >= 'A' && c3 <= 'Z') c3 += 32;
        if (c0 == 'h' && c1 == 'r' && c2 == 'e' && c3 == 'f') {
            /* Skip to = */
            int j = i + 4;
            while (j < body_len && tag_body[j] == ' ') j++;
            if (j < body_len && tag_body[j] == '=') {
                j++;
                while (j < body_len && tag_body[j] == ' ') j++;
                char quote = 0;
                if (j < body_len && (tag_body[j] == '"' || tag_body[j] == '\'')) {
                    quote = tag_body[j]; j++;
                }
                int k = 0;
                while (j < body_len && k < max - 1) {
                    if (quote && tag_body[j] == quote) break;
                    if (!quote && (tag_body[j] == ' ' || tag_body[j] == '>')) break;
                    href_out[k++] = tag_body[j++];
                }
                href_out[k] = '\0';
            }
            return;
        }
    }
}

/* ---- Page loading ---- */

static void load_page(const char *filename) {
    /* Push current page to history before navigating */
    if (addr_len > 0 && history_count < HISTORY_MAX) {
        str_copy(history[history_count], addr_buf);
        history_count++;
    }

    /* Update address bar */
    int i = 0;
    while (filename[i] && i < ADDR_MAX) { addr_buf[i] = filename[i]; i++; }
    addr_buf[i] = '\0';
    addr_len = i;

    /* Load file */
    int idx = fs_find(filename);
    if (idx < 0) {
        /* File not found - show error page */
        const char *err = "<h1>File Not Found</h1><p>Could not find: ";
        int elen = str_len(err);
        mem_copy(page_buf, err, (uint32_t)elen);
        int pos = elen;
        for (int j = 0; filename[j] && pos < PAGE_BUF_SIZE - 10; j++)
            page_buf[pos++] = filename[j];
        page_buf[pos++] = '<'; page_buf[pos++] = '/'; page_buf[pos++] = 'p'; page_buf[pos++] = '>';
        page_len = pos;
        page_buf[page_len] = '\0';
    } else {
        struct fs_node *f = fs_get_node(idx);
        if (!f || f->type != FS_FILE || !f->data) {
            const char *err = "<h1>Error</h1><p>Cannot read file.</p>";
            page_len = str_len(err);
            mem_copy(page_buf, err, (uint32_t)page_len);
        } else {
            int len = (int)f->size;
            if (len > PAGE_BUF_SIZE - 1) len = PAGE_BUF_SIZE - 1;
            mem_copy(page_buf, f->data, (uint32_t)len);
            page_len = len;
        }
        page_buf[page_len] = '\0';
    }

    scroll_y = 0;
    link_count = 0;
    hovered_link = -1;
    str_copy(status_msg, "Ready");
}

static void navigate_back(void) {
    if (history_count <= 0) return;
    history_count--;

    /* Load without pushing to history again */
    int i = 0;
    const char *filename = history[history_count];
    while (filename[i] && i < ADDR_MAX) { addr_buf[i] = filename[i]; i++; }
    addr_buf[i] = '\0';
    addr_len = i;

    int idx = fs_find(filename);
    if (idx >= 0) {
        struct fs_node *f = fs_get_node(idx);
        if (f && f->type == FS_FILE && f->data) {
            int len = (int)f->size;
            if (len > PAGE_BUF_SIZE - 1) len = PAGE_BUF_SIZE - 1;
            mem_copy(page_buf, f->data, (uint32_t)len);
            page_len = len;
            page_buf[page_len] = '\0';
        }
    }

    scroll_y = 0;
    link_count = 0;
    hovered_link = -1;
    str_copy(status_msg, "Ready");
}

/* ---- Event handler ---- */

static void brw_on_event(struct window *win, struct gui_event *evt) {
    if (evt->type == EVT_KEY_PRESS) {
        uint8_t k = evt->key;

        if (addr_focused) {
            /* Address bar input */
            if (k == '\n') {
                if (addr_len > 0) {
                    addr_buf[addr_len] = '\0';
                    /* Don't push to history here - load_page does it */
                    char tmp[ADDR_MAX + 1];
                    str_copy(tmp, addr_buf);
                    /* Reset history push by clearing addr_len briefly */
                    int saved_len = addr_len;
                    addr_len = 0;
                    load_page(tmp);
                    (void)saved_len;
                }
                addr_focused = false;
                return;
            }
            if (k == 0x1B) { addr_focused = false; return; }
            if (k == '\b') { if (addr_len > 0) addr_len--; return; }
            if (k >= 0x20 && k < 0x80 && addr_len < ADDR_MAX) {
                addr_buf[addr_len++] = (char)k;
                return;
            }
            return;
        }

        /* Content area navigation */
        if (k == KEY_UP)   { scroll_y -= 16; if (scroll_y < 0) scroll_y = 0; return; }
        if (k == KEY_DOWN) { scroll_y += 16; return; }
        if (k == KEY_PGUP) { scroll_y -= CONTENT_H; if (scroll_y < 0) scroll_y = 0; return; }
        if (k == KEY_PGDN) { scroll_y += CONTENT_H; return; }
        if (k == KEY_HOME) { scroll_y = 0; return; }
        if (k == KEY_END)  { scroll_y = content_total_h - CONTENT_H; if (scroll_y < 0) scroll_y = 0; return; }

        /* Alt+Left or Backspace = back */
        if (k == '\b') { navigate_back(); return; }

        /* Tab focuses address bar */
        if (k == '\t') { addr_focused = true; return; }

        /* L key (Ctrl+L) focus address bar */
        if (k == 12) { addr_focused = true; addr_len = 0; return; }

        return;
    }

    if (evt->type == EVT_MOUSE_DOWN) {
        int mx = evt->mouse_x - (win->x + BORDER_WIDTH);
        int my = evt->mouse_y - (win->y + TITLEBAR_HEIGHT);

        /* Back button */
        if (mx >= 4 && mx < 34 && my >= 4 && my < 22) {
            navigate_back();
            return;
        }

        /* Go button */
        if (mx >= BRW_W - 36 && mx < BRW_W - 4 && my >= 4 && my < 22) {
            if (addr_len > 0) {
                addr_buf[addr_len] = '\0';
                char tmp[ADDR_MAX + 1];
                str_copy(tmp, addr_buf);
                addr_len = 0;
                load_page(tmp);
            }
            return;
        }

        /* Address bar click */
        if (mx >= ADDR_X && mx < ADDR_X + ADDR_W && my >= 4 && my < 22) {
            addr_focused = true;
            return;
        }

        /* Content area - check link clicks */
        if (my >= CONTENT_Y && my < CONTENT_Y + CONTENT_H) {
            int content_mx = mx;
            int content_my = my - CONTENT_Y + scroll_y;

            for (int i = 0; i < link_count; i++) {
                if (content_mx >= links[i].x && content_mx < links[i].x + links[i].w &&
                    content_my >= links[i].y && content_my < links[i].y + links[i].h) {
                    load_page(links[i].href);
                    return;
                }
            }

            /* Click in content unfocuses address bar */
            addr_focused = false;
        }
    }

    if (evt->type == EVT_MOUSE_MOVE) {
        int mx = evt->mouse_x - (win->x + BORDER_WIDTH);
        int my = evt->mouse_y - (win->y + TITLEBAR_HEIGHT);

        hovered_link = -1;
        if (my >= CONTENT_Y && my < CONTENT_Y + CONTENT_H) {
            int content_mx = mx;
            int content_my = my - CONTENT_Y + scroll_y;

            for (int i = 0; i < link_count; i++) {
                if (content_mx >= links[i].x && content_mx < links[i].x + links[i].w &&
                    content_my >= links[i].y && content_my < links[i].y + links[i].h) {
                    hovered_link = i;
                    str_copy(status_msg, links[i].href);
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
    win_id = wm_create_window("Browser", 40, 30, BRW_W, BRW_H,
                               brw_on_event, NULL);
    str_copy(status_msg, "Ready");

    /* Load home page */
    addr_len = 0;
    history_count = 0;
    load_page("home.htm");
}

bool browser_is_alive(void) {
    if (win_id < 0) return false;
    struct window *w = wm_get_window(win_id);
    return (w && w->alive);
}

void browser_render(void) {
    if (win_id < 0) return;
    struct window *win = wm_get_window(win_id);
    if (!win || !win->alive || !win->content || win->on_event != brw_on_event) { win_id = -1; return; }

    int cw = win->content_w;
    int ch = win->content_h;
    color_t *buf = win->content;

    /* Clear */
    for (int i = 0; i < cw * ch; i++) buf[i] = C_BG;

    /* ---- Toolbar ---- */
    brw_rect(buf, cw, ch, 0, 0, cw, TOOLBAR_H, C_TOOLBAR);

    /* Back button */
    brw_rect(buf, cw, ch, 4, 4, 30, 18, C_BTN_BG);
    brw_text(buf, cw, ch, 8, 5, "<-", C_BTN_FG, C_BTN_BG);

    /* Address bar */
    brw_rect(buf, cw, ch, ADDR_X, 4, ADDR_W, 18, C_ADDR_BG);
    /* Border */
    brw_rect(buf, cw, ch, ADDR_X, 4, ADDR_W, 1, COLOR_RGB(180, 180, 180));
    brw_rect(buf, cw, ch, ADDR_X, 21, ADDR_W, 1, COLOR_RGB(180, 180, 180));
    brw_rect(buf, cw, ch, ADDR_X, 4, 1, 18, COLOR_RGB(180, 180, 180));
    brw_rect(buf, cw, ch, ADDR_X + ADDR_W - 1, 4, 1, 18, COLOR_RGB(180, 180, 180));

    /* Address text */
    addr_buf[addr_len] = '\0';
    if (addr_len > 0) {
        /* Show as many chars as fit */
        int max_chars = (ADDR_W - 8) / 8;
        int start = 0;
        if (addr_len > max_chars) start = addr_len - max_chars;
        brw_text(buf, cw, ch, ADDR_X + 4, 5, addr_buf + start, C_ADDR_FG, C_ADDR_BG);
    }

    /* Cursor in address bar */
    if (addr_focused && (timer_get_ticks() / 40) & 1) {
        int max_chars = (ADDR_W - 8) / 8;
        int visible_len = addr_len;
        if (visible_len > max_chars) visible_len = max_chars;
        brw_rect(buf, cw, ch, ADDR_X + 4 + visible_len * 8, 5, 2, 16, COLOR_RGB(0, 80, 200));
    }

    /* Go button */
    brw_rect(buf, cw, ch, BRW_W - 36, 4, 32, 18, C_BTN_BG);
    brw_text(buf, cw, ch, BRW_W - 32, 5, "Go", C_BTN_FG, C_BTN_BG);

    /* ---- Render HTML content ---- */
    link_count = 0;

    /* Parser state */
    int draw_x = 8;
    int draw_y = 4;
    int max_x = BRW_W - 16;
    bool in_bold = false;
    bool in_italic = false;
    bool in_underline = false;
    bool in_link = false;
    int heading = 0;  /* 0=none, 1=h1, 2=h2, 3=h3 */
    bool in_list = false;
    bool at_li_start = false;
    char current_href[ADDR_MAX + 1];
    int link_start_x = 0;
    int link_start_y = 0;
    current_href[0] = '\0';

    int pos = 0;
    while (pos < page_len) {
        if (page_buf[pos] == '<') {
            /* Parse tag */
            pos++;
            bool closing = false;
            if (pos < page_len && page_buf[pos] == '/') { closing = true; pos++; }

            /* Read tag name + attributes */
            char tag_full[128];
            int tf = 0;
            while (pos < page_len && page_buf[pos] != '>' && tf < 127) {
                tag_full[tf++] = page_buf[pos++];
            }
            tag_full[tf] = '\0';
            if (pos < page_len && page_buf[pos] == '>') pos++;

            /* Extract just the tag name (up to first space) */
            char tag_name[16];
            int tn = 0;
            for (int i = 0; i < tf && tag_full[i] != ' ' && tn < 15; i++)
                tag_name[tn++] = tag_full[i];
            tag_name[tn] = '\0';

            /* Process tag */
            if (tag_eq(tag_name, tn, "h1")) {
                if (!closing) { heading = 1; draw_y += 8; draw_x = 8; }
                else { heading = 0; draw_y += 20; draw_x = 8; }
            } else if (tag_eq(tag_name, tn, "h2")) {
                if (!closing) { heading = 2; draw_y += 6; draw_x = 8; }
                else { heading = 0; draw_y += 18; draw_x = 8; }
            } else if (tag_eq(tag_name, tn, "h3")) {
                if (!closing) { heading = 3; draw_y += 4; draw_x = 8; }
                else { heading = 0; draw_y += 16; draw_x = 8; }
            } else if (tag_eq(tag_name, tn, "p")) {
                if (!closing) { draw_y += 6; draw_x = 8; }
                else { draw_y += 8; draw_x = 8; }
            } else if (tag_eq(tag_name, tn, "b") || tag_eq(tag_name, tn, "strong")) {
                in_bold = !closing;
            } else if (tag_eq(tag_name, tn, "i") || tag_eq(tag_name, tn, "em")) {
                in_italic = !closing;
            } else if (tag_eq(tag_name, tn, "u")) {
                in_underline = !closing;
            } else if (tag_eq(tag_name, tn, "br")) {
                draw_y += 16;
                draw_x = 8;
            } else if (tag_eq(tag_name, tn, "hr")) {
                draw_y += 4;
                /* Draw horizontal rule in content region */
                int rule_y = CONTENT_Y + draw_y - scroll_y;
                if (rule_y >= CONTENT_Y && rule_y < CONTENT_Y + CONTENT_H)
                    brw_rect(buf, cw, ch, 8, rule_y, BRW_W - 16, 1, C_HR);
                draw_y += 8;
                draw_x = 8;
            } else if (tag_eq(tag_name, tn, "a")) {
                if (!closing) {
                    in_link = true;
                    extract_href(tag_full, tf, current_href, ADDR_MAX);
                    link_start_x = draw_x;
                    link_start_y = draw_y;
                } else {
                    /* Register link region */
                    if (in_link && link_count < MAX_LINKS && current_href[0]) {
                        links[link_count].x = link_start_x;
                        links[link_count].y = link_start_y;
                        links[link_count].w = draw_x - link_start_x;
                        links[link_count].h = 16;
                        str_ncopy(links[link_count].href, current_href, ADDR_MAX);
                        link_count++;
                    }
                    in_link = false;
                    current_href[0] = '\0';
                }
            } else if (tag_eq(tag_name, tn, "ul") || tag_eq(tag_name, tn, "ol")) {
                if (!closing) { in_list = true; draw_y += 4; }
                else { in_list = false; draw_y += 4; }
            } else if (tag_eq(tag_name, tn, "li")) {
                if (!closing) {
                    draw_y += 2;
                    draw_x = in_list ? 24 : 8;
                    at_li_start = true;
                } else {
                    draw_y += 16;
                    draw_x = 8;
                }
            }
            /* Skip unknown tags */
            continue;
        }

        /* Regular character */
        char c = page_buf[pos++];

        /* Skip whitespace runs */
        if ((c == '\n' || c == '\r') && pos < page_len) continue;

        /* Determine color */
        color_t fg;
        if (in_link) {
            fg = C_LINK;
        } else if (heading == 1) {
            fg = C_H1;
        } else if (heading == 2) {
            fg = C_H2;
        } else if (heading == 3) {
            fg = C_H3;
        } else if (in_bold) {
            fg = C_BOLD;
        } else if (in_italic) {
            fg = C_ITALIC;
        } else {
            fg = C_TEXT;
        }

        /* Word wrap */
        if (draw_x + 8 > max_x) {
            draw_y += 16;
            draw_x = in_list ? 24 : 8;
        }

        /* Draw bullet at start of list item */
        if (at_li_start) {
            int bullet_sy = CONTENT_Y + draw_y - scroll_y;
            if (bullet_sy >= CONTENT_Y && bullet_sy + 16 <= CONTENT_Y + CONTENT_H) {
                /* Small filled circle (just a square for simplicity) */
                brw_rect(buf, cw, ch, draw_x - 12, bullet_sy + 6, 4, 4, C_BULLET);
            }
            at_li_start = false;
        }

        /* Draw character if visible */
        int screen_y = CONTENT_Y + draw_y - scroll_y;
        if (screen_y >= CONTENT_Y - 16 && screen_y < CONTENT_Y + CONTENT_H) {
            /* Clip to content area */
            if (screen_y >= CONTENT_Y && screen_y + 16 <= CONTENT_Y + CONTENT_H) {
                brw_char_transparent(buf, cw, ch, draw_x, screen_y, c, fg);
                if (in_underline || in_link)
                    brw_underline(buf, cw, ch, draw_x, screen_y, 8, fg);
            }
        }

        draw_x += 8;
    }

    /* Record total content height for scroll clamping */
    content_total_h = draw_y + 20;
    if (scroll_y > content_total_h - CONTENT_H) {
        if (content_total_h > CONTENT_H)
            scroll_y = content_total_h - CONTENT_H;
        else
            scroll_y = 0;
    }

    /* ---- Status bar ---- */
    brw_rect(buf, cw, ch, 0, BRW_H - STATUS_H, BRW_W, STATUS_H, C_STATUS_BG);
    brw_text(buf, cw, ch, 4, BRW_H - STATUS_H + 1, status_msg, C_STATUS_FG, C_STATUS_BG);

    /* Scroll indicator on right side */
    if (content_total_h > CONTENT_H) {
        int track_h = CONTENT_H - 4;
        int thumb_h = (CONTENT_H * track_h) / content_total_h;
        if (thumb_h < 10) thumb_h = 10;
        int thumb_y = CONTENT_Y + 2 + (scroll_y * (track_h - thumb_h)) / (content_total_h - CONTENT_H);
        brw_rect(buf, cw, ch, BRW_W - 6, CONTENT_Y + 2, 4, track_h, COLOR_RGB(210, 210, 215));
        brw_rect(buf, cw, ch, BRW_W - 6, thumb_y, 4, thumb_h, COLOR_RGB(160, 160, 170));
    }
}
