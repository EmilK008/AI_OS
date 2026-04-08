/* ===========================================================================
 * Built-in Text Editor (nano-like)
 * Full-screen editor with arrow key navigation
 * =========================================================================== */
#include "editor.h"
#include "vga.h"
#include "keyboard.h"
#include "fs.h"
#include "string.h"
#include "io.h"

#define EDIT_MAX_LINES 100
#define EDIT_MAX_COLS  79
#define EDIT_ROWS      23    /* rows for text (25 - status bar - header) */

static char lines[EDIT_MAX_LINES][EDIT_MAX_COLS + 1];
static int line_count;
static int cur_row, cur_col;
static int scroll_off;
static char edit_filename[MAX_FILENAME];
static int file_idx;
static bool modified;

static void editor_draw_status(void) {
    uint8_t color = VGA_COLOR(VGA_BLACK, VGA_LIGHT_GREY);
    /* Header bar (row 0) */
    uint16_t *vga = (uint16_t *)0xB8000;
    for (int x = 0; x < 80; x++) {
        vga[x] = (uint16_t)color << 8 | ' ';
    }
    /* Title */
    const char *title = " AI_OS Editor";
    for (int i = 0; title[i]; i++) {
        vga[i] = (uint16_t)color << 8 | title[i];
    }
    /* Filename */
    int fx = 15;
    const char *dash = "- ";
    for (int i = 0; dash[i] && fx < 80; i++) {
        vga[fx++] = (uint16_t)color << 8 | dash[i];
    }
    for (int i = 0; edit_filename[i] && fx < 80; i++) {
        vga[fx++] = (uint16_t)color << 8 | edit_filename[i];
    }
    if (modified && fx < 80) {
        vga[fx++] = (uint16_t)color << 8 | '*';
    }

    /* Bottom status bar (row 24) */
    int row24 = 24 * 80;
    for (int x = 0; x < 80; x++) {
        vga[row24 + x] = (uint16_t)color << 8 | ' ';
    }
    /* Show position and help */
    char pos_str[40];
    /* Manual sprintf: "Ln X, Col Y" */
    int p = 0;
    pos_str[p++] = 'L'; pos_str[p++] = 'n'; pos_str[p++] = ' ';
    /* Line number */
    int ln = cur_row + 1;
    char num[8]; int ni = 0;
    if (ln == 0) { num[ni++] = '0'; }
    else { int t = ln; while (t > 0) { num[ni++] = '0' + (t % 10); t /= 10; } }
    for (int i = ni - 1; i >= 0; i--) pos_str[p++] = num[i];
    pos_str[p++] = ','; pos_str[p++] = ' ';
    pos_str[p++] = 'C'; pos_str[p++] = 'o'; pos_str[p++] = 'l'; pos_str[p++] = ' ';
    int cn = cur_col + 1; ni = 0;
    if (cn == 0) { num[ni++] = '0'; }
    else { int t = cn; while (t > 0) { num[ni++] = '0' + (t % 10); t /= 10; } }
    for (int i = ni - 1; i >= 0; i--) pos_str[p++] = num[i];
    pos_str[p] = '\0';

    for (int i = 0; pos_str[i] && i < 20; i++) {
        vga[row24 + i] = (uint16_t)color << 8 | pos_str[i];
    }

    const char *help = "^S Save  ^Q Quit";
    int hx = 80 - 17;
    for (int i = 0; help[i] && hx < 80; i++) {
        vga[row24 + hx++] = (uint16_t)color << 8 | help[i];
    }
}

static void editor_draw(void) {
    uint16_t *vga = (uint16_t *)0xB8000;
    uint8_t text_color = VGA_COLOR(VGA_LIGHT_GREY, VGA_BLACK);
    uint8_t ln_color = VGA_COLOR(VGA_DARK_GREY, VGA_BLACK);
    uint8_t tilde_color = VGA_COLOR(VGA_BLUE, VGA_BLACK);

    /* Draw text area (rows 1-23) */
    for (int row = 0; row < EDIT_ROWS; row++) {
        int file_line = row + scroll_off;
        int vga_row = (row + 1) * 80;

        /* Clear the row */
        for (int x = 0; x < 80; x++) {
            vga[vga_row + x] = (uint16_t)text_color << 8 | ' ';
        }

        if (file_line < line_count) {
            /* Show line number (4 chars) */
            int lnum = file_line + 1;
            char nb[5] = "    ";
            int d = 3;
            while (lnum > 0 && d >= 0) {
                nb[d--] = '0' + (lnum % 10);
                lnum /= 10;
            }
            for (int i = 0; i < 4; i++) {
                vga[vga_row + i] = (uint16_t)ln_color << 8 | nb[i];
            }
            vga[vga_row + 4] = (uint16_t)ln_color << 8 | ' ';

            /* Show line content */
            for (int x = 0; lines[file_line][x] && x + 5 < 80; x++) {
                vga[vga_row + x + 5] = (uint16_t)text_color << 8 | lines[file_line][x];
            }
        } else {
            /* Empty line marker */
            vga[vga_row] = (uint16_t)tilde_color << 8 | '~';
        }
    }

    editor_draw_status();

    /* Set cursor position */
    int screen_row = cur_row - scroll_off + 1;
    int screen_col = cur_col + 5;
    uint16_t cursor_pos = screen_row * 80 + screen_col;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(cursor_pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((cursor_pos >> 8) & 0xFF));
}

static void editor_scroll_to_cursor(void) {
    if (cur_row < scroll_off) {
        scroll_off = cur_row;
    }
    if (cur_row >= scroll_off + EDIT_ROWS) {
        scroll_off = cur_row - EDIT_ROWS + 1;
    }
}

static void editor_insert_char(char c) {
    int len = str_len(lines[cur_row]);
    if (len >= EDIT_MAX_COLS) return;

    /* Shift characters right */
    for (int i = len + 1; i > cur_col; i--) {
        lines[cur_row][i] = lines[cur_row][i - 1];
    }
    lines[cur_row][cur_col] = c;
    cur_col++;
    modified = true;
}

static void editor_insert_line(void) {
    if (line_count >= EDIT_MAX_LINES) return;

    /* Shift lines down */
    for (int i = line_count; i > cur_row + 1; i--) {
        str_copy(lines[i], lines[i - 1]);
    }
    line_count++;

    /* Split current line at cursor */
    lines[cur_row + 1][0] = '\0';
    int old_len = str_len(lines[cur_row]);
    if (cur_col < old_len) {
        str_copy(lines[cur_row + 1], lines[cur_row] + cur_col);
        lines[cur_row][cur_col] = '\0';
    }

    cur_row++;
    cur_col = 0;
    modified = true;
}

static void editor_delete_char(void) {
    if (cur_col > 0) {
        int len = str_len(lines[cur_row]);
        for (int i = cur_col - 1; i < len; i++) {
            lines[cur_row][i] = lines[cur_row][i + 1];
        }
        cur_col--;
        modified = true;
    } else if (cur_row > 0) {
        /* Merge with previous line */
        int prev_len = str_len(lines[cur_row - 1]);
        int cur_len = str_len(lines[cur_row]);
        if (prev_len + cur_len < EDIT_MAX_COLS) {
            str_copy(lines[cur_row - 1] + prev_len, lines[cur_row]);
            /* Shift lines up */
            for (int i = cur_row; i < line_count - 1; i++) {
                str_copy(lines[i], lines[i + 1]);
            }
            line_count--;
            cur_row--;
            cur_col = prev_len;
            modified = true;
        }
    }
}

static void editor_save(void) {
    /* Build file content from lines */
    char buf[MAX_FILE_DATA];
    int pos = 0;
    for (int i = 0; i < line_count && pos < MAX_FILE_DATA - 2; i++) {
        for (int j = 0; lines[i][j] && pos < MAX_FILE_DATA - 2; j++) {
            buf[pos++] = lines[i][j];
        }
        if (i < line_count - 1) {
            buf[pos++] = '\n';
        }
    }
    buf[pos] = '\0';

    if (file_idx < 0) {
        /* Create new file */
        file_idx = fs_create(edit_filename, FS_FILE);
    }
    if (file_idx >= 0) {
        fs_write_file(file_idx, buf, pos);
        modified = false;
    }
}

static void editor_load(void) {
    line_count = 1;
    lines[0][0] = '\0';
    cur_row = 0;
    cur_col = 0;
    scroll_off = 0;
    modified = false;

    if (file_idx < 0) return;

    char buf[MAX_FILE_DATA];
    int len = fs_read_file(file_idx, buf, MAX_FILE_DATA);
    if (len <= 0) return;

    /* Parse into lines */
    line_count = 0;
    int col = 0;
    for (int i = 0; i < len; i++) {
        if (buf[i] == '\n' || col >= EDIT_MAX_COLS) {
            lines[line_count][col] = '\0';
            line_count++;
            col = 0;
            if (line_count >= EDIT_MAX_LINES) break;
        } else {
            lines[line_count][col++] = buf[i];
        }
    }
    if (col > 0 || line_count == 0) {
        lines[line_count][col] = '\0';
        line_count++;
    }
}

void editor_open(const char *filename) {
    str_copy(edit_filename, filename);
    file_idx = fs_find(filename);

    /* Clear all lines */
    for (int i = 0; i < EDIT_MAX_LINES; i++) {
        lines[i][0] = '\0';
    }

    editor_load();
    editor_draw();

    /* Editor main loop */
    while (1) {
        char c = keyboard_getchar();

        if (c == 19) {
            /* Ctrl+S: Save */
            editor_save();
            editor_draw();
            continue;
        }

        if (c == 17) {
            /* Ctrl+Q: Quit */
            break;
        }

        /* Arrow keys (special codes from keyboard driver) */
        if ((uint8_t)c == 0x80) {
            /* Up */
            if (cur_row > 0) {
                cur_row--;
                int len = str_len(lines[cur_row]);
                if (cur_col > len) cur_col = len;
            }
        } else if ((uint8_t)c == 0x81) {
            /* Down */
            if (cur_row < line_count - 1) {
                cur_row++;
                int len = str_len(lines[cur_row]);
                if (cur_col > len) cur_col = len;
            }
        } else if ((uint8_t)c == 0x82) {
            /* Left */
            if (cur_col > 0) {
                cur_col--;
            } else if (cur_row > 0) {
                cur_row--;
                cur_col = str_len(lines[cur_row]);
            }
        } else if ((uint8_t)c == 0x83) {
            /* Right */
            int len = str_len(lines[cur_row]);
            if (cur_col < len) {
                cur_col++;
            } else if (cur_row < line_count - 1) {
                cur_row++;
                cur_col = 0;
            }
        } else if (c == '\n') {
            editor_insert_line();
        } else if (c == '\b') {
            editor_delete_char();
        } else if (c == '\t') {
            /* Insert 4 spaces */
            for (int i = 0; i < 4; i++) editor_insert_char(' ');
        } else if (c >= 32 && c < 127) {
            editor_insert_char(c);
        }

        editor_scroll_to_cursor();
        editor_draw();
    }

    /* Restore VGA state */
    vga_clear();
}
