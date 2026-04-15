/* ===========================================================================
 * Framebuffer Driver - Pixel-mode VGA via Bochs VBE (BGA)
 * Double-buffered 640x480x32 rendering for QEMU
 * =========================================================================== */
#include "framebuffer.h"
#include "font8x16.h"
#include "memory.h"
#include "string.h"
#include "io.h"

/* Bochs VBE / VGA Adapter registers (available in QEMU -vga std) */
#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA  0x01CF

#define VBE_DISPI_INDEX_ID          0x00
#define VBE_DISPI_INDEX_XRES        0x01
#define VBE_DISPI_INDEX_YRES        0x02
#define VBE_DISPI_INDEX_BPP         0x03
#define VBE_DISPI_INDEX_ENABLE      0x04
#define VBE_DISPI_INDEX_BANK        0x05
#define VBE_DISPI_INDEX_VIRT_WIDTH  0x06
#define VBE_DISPI_INDEX_VIRT_HEIGHT 0x07
#define VBE_DISPI_INDEX_X_OFFSET    0x08
#define VBE_DISPI_INDEX_Y_OFFSET    0x09
#define VBE_DISPI_INDEX_VIDEO_MEMORY_64K 0x0A

#define VBE_DISPI_DISABLED   0x00
#define VBE_DISPI_ENABLED    0x01
#define VBE_DISPI_LFB_ENABLED 0x40

/* The BGA framebuffer is at a fixed address in QEMU's default PCI layout */
#define BGA_LFB_ADDRESS 0xE0000000

static uint32_t screen_width, screen_height, screen_pitch, screen_bpp;
static color_t *vram;
static color_t *backbuf;
static uint32_t fb_size;
static bool     initialized = false;

static void bga_write(uint16_t index, uint16_t value) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

static uint16_t bga_read(uint16_t index) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

static inline void fast_memcpy32(void *dst, const void *src, uint32_t bytes) {
    uint32_t dwords = bytes >> 2;
    __asm__ __volatile__(
        "rep movsl"
        : "+D"(dst), "+S"(src), "+c"(dwords)
        :
        : "memory"
    );
}

static inline void fast_memset32(void *dst, uint32_t val, uint32_t count) {
    __asm__ __volatile__(
        "rep stosl"
        : "+D"(dst), "+c"(count)
        : "a"(val)
        : "memory"
    );
}

/* Read 32-bit value from PCI configuration space */
static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (offset & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

/* Write 32-bit value to PCI configuration space */
static void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (offset & 0xFC);
    outl(0xCF8, addr);
    outl(0xCFC, value);
}

void fb_init(void) {
    /* Detect BGA: write a version ID and read it back */
    bga_write(VBE_DISPI_INDEX_ID, 0xB0C4);
    uint16_t bga_id = bga_read(VBE_DISPI_INDEX_ID);
    if (bga_id < 0xB0C0 || bga_id > 0xB0CF) {
        /* Try oldest version */
        bga_write(VBE_DISPI_INDEX_ID, 0xB0C0);
        bga_id = bga_read(VBE_DISPI_INDEX_ID);
        if (bga_id < 0xB0C0 || bga_id > 0xB0CF) return;
    }

    /* Verify PCI device: Bochs VGA = vendor 0x1234, device 0x1111 */
    uint32_t pci_id = pci_read32(0, 2, 0, 0x00);
    if (pci_id != 0x11111234) return;

    /* Read BAR0 for LFB address */
    uint32_t bar0 = pci_read32(0, 2, 0, 0x10);
    if ((bar0 & 0xFFFFFFF0) == 0) {
        /* BAR0 not assigned - assign 0xE0000000 ourselves */
        pci_write32(0, 2, 0, 0x10, 0xE0000000);
        bar0 = pci_read32(0, 2, 0, 0x10);
    }
    bar0 &= 0xFFFFFFF0;
    if (bar0 == 0) return;

    /* Enable memory space + I/O space in PCI command register */
    uint32_t cmd = pci_read32(0, 2, 0, 0x04);
    cmd |= 0x03;
    pci_write32(0, 2, 0, 0x04, cmd);

    /* Set 640x480x32 mode */
    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    bga_write(VBE_DISPI_INDEX_XRES, 640);
    bga_write(VBE_DISPI_INDEX_YRES, 480);
    bga_write(VBE_DISPI_INDEX_BPP, 32);
    bga_write(VBE_DISPI_INDEX_VIRT_WIDTH, 640);
    bga_write(VBE_DISPI_INDEX_VIRT_HEIGHT, 480);
    bga_write(VBE_DISPI_INDEX_X_OFFSET, 0);
    bga_write(VBE_DISPI_INDEX_Y_OFFSET, 0);
    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    screen_width  = 640;
    screen_height = 480;
    screen_bpp    = 32;
    screen_pitch  = screen_width * 4;
    vram          = (color_t *)bar0;

    fb_size = screen_width * screen_height * sizeof(color_t);
    backbuf = (color_t *)kmalloc(fb_size);
    if (!backbuf) return;

    mem_set(backbuf, 0, fb_size);
    initialized = true;
}

bool fb_available(void) {
    return initialized;
}

void fb_putpixel(int x, int y, color_t color) {
    if (x < 0 || x >= (int)screen_width || y < 0 || y >= (int)screen_height) return;
    backbuf[y * (screen_pitch / 4) + x] = color;
}

void fb_fill_rect(int x, int y, int w, int h, color_t color) {
    /* Clip */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)screen_width)  w = (int)screen_width - x;
    if (y + h > (int)screen_height) h = (int)screen_height - y;
    if (w <= 0 || h <= 0) return;

    uint32_t stride = screen_pitch / 4;
    color_t *row = backbuf + y * stride + x;
    for (int j = 0; j < h; j++) {
        fast_memset32(row, color, (uint32_t)w);
        row += stride;
    }
}

void fb_draw_rect(int x, int y, int w, int h, color_t color) {
    fb_fill_rect(x, y, w, 1, color);         /* top */
    fb_fill_rect(x, y + h - 1, w, 1, color); /* bottom */
    fb_fill_rect(x, y, 1, h, color);         /* left */
    fb_fill_rect(x + w - 1, y, 1, h, color); /* right */
}

void fb_draw_char(int x, int y, char c, color_t fg, color_t bg) {
    const uint8_t *glyph = font8x16[(uint8_t)c];
    uint32_t stride = screen_pitch / 4;

    for (int row = 0; row < 16; row++) {
        int py = y + row;
        if (py < 0 || py >= (int)screen_height) continue;
        uint8_t bits = glyph[row];
        color_t *pixel = backbuf + py * stride + x;
        for (int col = 0; col < 8; col++) {
            int px = x + col;
            if (px >= 0 && px < (int)screen_width) {
                *pixel = (bits & (0x80 >> col)) ? fg : bg;
            }
            pixel++;
        }
    }
}

void fb_draw_char_transparent(int x, int y, char c, color_t fg) {
    const uint8_t *glyph = font8x16[(uint8_t)c];
    uint32_t stride = screen_pitch / 4;

    for (int row = 0; row < 16; row++) {
        int py = y + row;
        if (py < 0 || py >= (int)screen_height) continue;
        uint8_t bits = glyph[row];
        color_t *pixel = backbuf + py * stride + x;
        for (int col = 0; col < 8; col++) {
            int px = x + col;
            if (px >= 0 && px < (int)screen_width && (bits & (0x80 >> col))) {
                *pixel = fg;
            }
            pixel++;
        }
    }
}

void fb_draw_string(int x, int y, const char *str, color_t fg, color_t bg) {
    while (*str) {
        fb_draw_char(x, y, *str, fg, bg);
        x += 8;
        str++;
    }
}

void fb_blit(int dst_x, int dst_y, int w, int h, const color_t *src, int src_pitch) {
    uint32_t stride = screen_pitch / 4;

    for (int j = 0; j < h; j++) {
        int py = dst_y + j;
        if (py < 0 || py >= (int)screen_height) continue;

        int sx = 0, dx = dst_x, cw = w;
        if (dx < 0) { sx = -dx; dx = 0; cw += dst_x; }
        if (dx + cw > (int)screen_width) cw = (int)screen_width - dx;
        if (cw <= 0) continue;

        const color_t *srow = src + j * src_pitch + sx;
        color_t *drow = backbuf + py * stride + dx;
        fast_memcpy32(drow, srow, (uint32_t)(cw * 4));
    }
}

void fb_flip(void) {
    if (!initialized) return;
    fast_memcpy32(vram, backbuf, fb_size);
}

void fb_clear(color_t color) {
    if (!initialized) return;
    fast_memset32(backbuf, color, screen_width * screen_height);
}

int fb_get_width(void)  { return (int)screen_width; }
int fb_get_height(void) { return (int)screen_height; }
color_t *fb_get_backbuf(void) { return backbuf; }

/* Debug output helpers for fb_set_mode */
static void fb_dbg_putc(char c) {
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)c), "Nd"((uint16_t)0xE9));
}
static void fb_dbg_print(const char *s) { while (*s) fb_dbg_putc(*s++); }
static void fb_dbg_dec(int v) {
    if (v < 0) { fb_dbg_putc('-'); v = -v; }
    if (v == 0) { fb_dbg_putc('0'); return; }
    char tmp[12]; int i = 0;
    while (v > 0) { tmp[i++] = '0' + (v % 10); v /= 10; }
    while (i > 0) fb_dbg_putc(tmp[--i]);
}

bool fb_set_mode(int width, int height) {
    fb_dbg_print("FB: set_mode "); fb_dbg_dec(width); fb_dbg_putc('x'); fb_dbg_dec(height); fb_dbg_putc('\n');
    if (!initialized) { fb_dbg_print("FB: not init\n"); return false; }
    if (width == (int)screen_width && height == (int)screen_height) { fb_dbg_print("FB: same mode\n"); return true; }

    /* Allocate new back buffer before touching hardware */
    uint32_t new_size = (uint32_t)width * (uint32_t)height * sizeof(color_t);
    fb_dbg_print("FB: alloc "); fb_dbg_dec((int)new_size); fb_dbg_putc('\n');
    color_t *new_buf = (color_t *)kmalloc(new_size);
    if (!new_buf) { fb_dbg_print("FB: alloc FAIL\n"); return false; }
    mem_set(new_buf, 0, new_size);

    /* Reprogram BGA hardware */
    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    bga_write(VBE_DISPI_INDEX_XRES, (uint16_t)width);
    bga_write(VBE_DISPI_INDEX_YRES, (uint16_t)height);
    bga_write(VBE_DISPI_INDEX_BPP, 32);
    bga_write(VBE_DISPI_INDEX_VIRT_WIDTH, (uint16_t)width);
    bga_write(VBE_DISPI_INDEX_VIRT_HEIGHT, (uint16_t)height);
    bga_write(VBE_DISPI_INDEX_X_OFFSET, 0);
    bga_write(VBE_DISPI_INDEX_Y_OFFSET, 0);
    bga_write(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    /* Free old back buffer and switch to new one */
    kfree(backbuf);
    backbuf = new_buf;
    fb_size = new_size;
    screen_width  = (uint32_t)width;
    screen_height = (uint32_t)height;
    screen_pitch  = screen_width * 4;

    return true;
}
