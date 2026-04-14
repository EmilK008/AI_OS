/* ===========================================================================
 * VGA Text Mode Driver
 * 80x25 color text output at 0xB8000
 * =========================================================================== */
#include "vga.h"
#include "io.h"
#include "terminal.h"

#define VGA_MEMORY  ((uint16_t*)0xB8000)
#define VGA_WIDTH   80
#define VGA_HEIGHT  25

static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t current_color = VGA_COLOR(VGA_LIGHT_GREY, VGA_BLACK);
static bool gui_mode = false;

void vga_set_gui_mode(bool enabled) { gui_mode = enabled; }

static void update_hw_cursor(void) {
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void scroll(void) {
    if (cursor_y >= VGA_HEIGHT) {
        for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
            VGA_MEMORY[i] = VGA_MEMORY[i + VGA_WIDTH];
        }
        uint16_t blank = (uint16_t)current_color << 8 | ' ';
        for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++) {
            VGA_MEMORY[i] = blank;
        }
        cursor_y = VGA_HEIGHT - 1;
    }
}

void vga_init(void) {
    current_color = VGA_COLOR(VGA_LIGHT_GREY, VGA_BLACK);
    vga_clear();
}

void vga_clear(void) {
    if (gui_mode) { terminal_clear_redirect(); return; }
    uint16_t blank = (uint16_t)current_color << 8 | ' ';
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_MEMORY[i] = blank;
    }
    cursor_x = 0;
    cursor_y = 0;
    update_hw_cursor();
}

void vga_set_color(uint8_t color) {
    if (gui_mode) { terminal_set_color_redirect(color); }
    current_color = color;
}

void vga_putchar(char c) {
    if (gui_mode) { terminal_putchar_redirect(c); return; }
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else if (c == '\r') {
        cursor_x = 0;
    } else if (c == '\t') {
        cursor_x = (cursor_x + 8) & ~7;
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    } else if (c == '\b') {
        if (cursor_x > 0) {
            cursor_x--;
            uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
            VGA_MEMORY[pos] = (uint16_t)current_color << 8 | ' ';
        }
    } else {
        uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
        VGA_MEMORY[pos] = (uint16_t)current_color << 8 | (uint16_t)c;
        cursor_x++;
        if (cursor_x >= VGA_WIDTH) {
            cursor_x = 0;
            cursor_y++;
        }
    }
    scroll();
    update_hw_cursor();
}

void vga_print(const char *str) {
    while (*str) {
        vga_putchar(*str++);
    }
}

void vga_print_colored(const char *str, uint8_t color) {
    uint8_t old = current_color;
    current_color = color;
    vga_print(str);
    current_color = old;
}

void vga_print_dec(uint32_t n) {
    if (n == 0) {
        vga_putchar('0');
        return;
    }
    char buf[12];
    int i = 0;
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (--i >= 0) {
        vga_putchar(buf[i]);
    }
}

void vga_print_hex(uint32_t n) {
    vga_print("0x");
    char hex[] = "0123456789ABCDEF";
    for (int i = 28; i >= 0; i -= 4) {
        vga_putchar(hex[(n >> i) & 0xF]);
    }
}

int vga_get_cursor_x(void) { return cursor_x; }
int vga_get_cursor_y(void) { return cursor_y; }

void vga_putchar_at(int x, int y, char c, uint8_t color) {
    if (gui_mode) { terminal_putchar_at_redirect(x, y, c, color); return; }
    if (x >= 0 && x < VGA_WIDTH && y >= 0 && y < VGA_HEIGHT) {
        VGA_MEMORY[y * VGA_WIDTH + x] = (uint16_t)color << 8 | (uint16_t)c;
    }
}

void vga_set_cursor_pos(int x, int y) {
    if (gui_mode) { terminal_set_cursor_redirect(x, y); return; }
    cursor_x = x;
    cursor_y = y;
    update_hw_cursor();
}
