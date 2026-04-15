/* ===========================================================================
 * AI_OS Kernel - Main
 * Entry point called from kernel_entry.asm
 * Initializes subsystems and launches GUI desktop
 * =========================================================================== */
#include "types.h"
#include "vga.h"
#include "idt.h"
#include "keyboard.h"
#include "timer.h"
#include "rtc.h"
#include "memory.h"
#include "shell.h"
#include "string.h"
#include "fs.h"
#include "process.h"
#include "framebuffer.h"
#include "mouse.h"
#include "event.h"
#include "window.h"
#include "desktop.h"
#include "terminal.h"
#include "io.h"
#include "snake_window.h"
#include "filemgr.h"
#include "calculator.h"
#include "notepad.h"
#include "paint.h"
#include "settings.h"
#include "ata.h"

/* Minimal debug output via QEMU debug port (0xE9) */
static void dbg_putc(char c) {
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)c), "Nd"((uint16_t)0xE9));
}

static void dbg_print(const char *s) {
    while (*s) dbg_putc(*s++);
}

void kernel_main(void) {
    dbg_print("AI_OS boot\n");

    memory_init();
    dbg_print("MEM ok\n");
    idt_init();
    dbg_print("IDT ok\n");
    timer_init();
    dbg_print("TMR ok\n");
    keyboard_init();
    event_init();
    mouse_init();
    dbg_print("KBD/EVT/MOUSE ok\n");
    rtc_init();

    /* Disk + Filesystem */
    bool have_disk = ata_init();
    dbg_print(have_disk ? "ATA ok\n" : "ATA: no disk\n");

    fs_init();
    if (have_disk && fs_load_from_disk() == 0) {
        dbg_print("DISK: loaded\n");
    } else {
        fs_init_defaults();
        if (have_disk) {
            fs_save_to_disk();
            dbg_print("DISK: formatted\n");
        }
    }
    fs_enable_autosave();

    process_init();
    dbg_print("FS/PROC ok\n");

    /* Enable interrupts now that all subsystems are ready */
    __asm__ __volatile__("sti");
    dbg_print("STI ok\n");

    /* Phase 2: Try framebuffer first */
    fb_init();
    dbg_print(fb_available() ? "FB: YES\n" : "FB: NO\n");

    if (fb_available()) {
        /* ===== GUI Mode ===== */

        /* Boot splash */
        dbg_print("GUI: clear\n");
        fb_clear(COLOR_BLACK);
        dbg_print("GUI: splash\n");
        fb_draw_string(232, 200, "AI_OS v0.3", COLOR_CYAN, COLOR_BLACK);
        fb_draw_string(216, 224, "Loading desktop...", COLOR_GREEN, COLOR_BLACK);
        fb_flip();
        dbg_print("GUI: flip done\n");

        /* Init GUI subsystems */
        mouse_set_bounds(fb_get_width(), fb_get_height());
        dbg_print("GUI: desktop\n");
        desktop_init();
        dbg_print("GUI: wm\n");
        wm_init();
        dbg_print("GUI: terminal\n");
        terminal_create();
        dbg_print("GUI: modes\n");

        /* Enable GUI mode on drivers */
        vga_set_gui_mode(true);
        keyboard_set_gui_mode(true);

        /* Spawn shell as separate process */
        dbg_print("GUI: spawn shell\n");
        process_create("shell", shell_entry);

        dbg_print("GUI: entering loop\n");
        /* GUI main loop (never returns) */
        while (1) {
            /* Process all pending events */
            struct gui_event evt;
            while (event_pop(&evt)) {
                wm_handle_event(&evt);
            }

            /* Render */
            terminal_render();
            snake_window_render();
            filemgr_render();
            calculator_render();
            notepad_render();
            paint_render();
            settings_render();
            wm_render_all();

            /* Yield CPU until next interrupt */
            __asm__ __volatile__("hlt");
        }
    } else {
        /* ===== Fallback: Text Mode (original behavior) ===== */
        /* Only now init VGA text mode (slow with -vga std due to MMIO) */
        vga_init();
        uint8_t banner_color = VGA_COLOR(VGA_LIGHT_CYAN, VGA_BLACK);
        uint8_t info_color   = VGA_COLOR(VGA_LIGHT_GREEN, VGA_BLACK);

        vga_print_colored("     ___    ____  ____  _____\n", banner_color);
        vga_print_colored("    /   |  /  _/ / __ \\/ ___/\n", banner_color);
        vga_print_colored("   / /| |  / /  / / / /\\__ \\ \n", banner_color);
        vga_print_colored("  / ___ |_/ /  / /_/ /___/ / \n", banner_color);
        vga_print_colored(" /_/  |_/___/  \\____//____/  \n", banner_color);
        vga_print("\n");
        vga_print_colored("  Built from scratch by Claude\n", info_color);
        vga_print_colored("  x86 Protected Mode Kernel v0.3\n\n", info_color);

        vga_print("  [*] Memory:  ");
        vga_print_dec(memory_get_free() / 1024);
        vga_print(" KB free\n");
        vga_print_colored("\n  VBE not available. Running in text mode.\n", VGA_COLOR(VGA_YELLOW, VGA_BLACK));
        vga_print_colored("  Type 'help' for commands.\n\n", VGA_COLOR(VGA_YELLOW, VGA_BLACK));

        shell_init();
        shell_run();

        while (1) { __asm__ __volatile__("hlt"); }
    }
}
