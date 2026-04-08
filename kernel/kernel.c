/* ===========================================================================
 * AI_OS Kernel - Main
 * Entry point called from kernel_entry.asm
 * =========================================================================== */
#include "types.h"
#include "vga.h"
#include "idt.h"
#include "keyboard.h"
#include "timer.h"
#include "memory.h"
#include "shell.h"
#include "string.h"
#include "fs.h"
#include "process.h"

void kernel_main(void) {
    /* Initialize VGA display */
    vga_init();

    /* Boot splash */
    uint8_t banner_color = VGA_COLOR(VGA_LIGHT_CYAN, VGA_BLACK);
    uint8_t info_color   = VGA_COLOR(VGA_LIGHT_GREEN, VGA_BLACK);
    uint8_t normal_color = VGA_COLOR(VGA_LIGHT_GREY, VGA_BLACK);

    vga_print_colored("     ___    ____  ____  _____\n", banner_color);
    vga_print_colored("    /   |  /  _/ / __ \\/ ___/\n", banner_color);
    vga_print_colored("   / /| |  / /  / / / /\\__ \\ \n", banner_color);
    vga_print_colored("  / ___ |_/ /  / /_/ /___/ / \n", banner_color);
    vga_print_colored(" /_/  |_/___/  \\____//____/  \n", banner_color);
    vga_print("\n");
    vga_print_colored("  Built from scratch by Claude\n", info_color);
    vga_print_colored("  x86 Protected Mode Kernel v0.2\n\n", info_color);

    /* Initialize subsystems */
    vga_print("  [");
    vga_print_colored("*", info_color);
    vga_print("] Memory manager........... ");
    memory_init();
    vga_print_colored("OK\n", info_color);

    vga_print("  [");
    vga_print_colored("*", info_color);
    vga_print("] Timer (100 Hz)........... ");
    timer_init();
    vga_print_colored("OK\n", info_color);

    vga_print("  [");
    vga_print_colored("*", info_color);
    vga_print("] IDT & exception handlers. ");
    idt_init();
    vga_print_colored("OK\n", info_color);

    vga_print("  [");
    vga_print_colored("*", info_color);
    vga_print("] Keyboard driver.......... ");
    keyboard_init();
    vga_print_colored("OK\n", info_color);

    vga_print("  [");
    vga_print_colored("*", info_color);
    vga_print("] RAM filesystem........... ");
    fs_init();
    vga_print_colored("OK\n", info_color);

    vga_print("  [");
    vga_print_colored("*", info_color);
    vga_print("] Process scheduler........ ");
    process_init();
    vga_print_colored("OK\n", info_color);

    vga_print("  [");
    vga_print_colored("*", info_color);
    vga_print("] Memory: ");
    vga_print_dec(memory_get_free() / 1024);
    vga_print(" KB free\n");

    vga_print("\n");
    vga_set_color(normal_color);
    vga_print_colored("  System ready. Type 'help' for commands.\n\n",
                      VGA_COLOR(VGA_YELLOW, VGA_BLACK));

    /* Launch shell */
    shell_init();
    shell_run();

    /* Should never reach here */
    while (1) {
        __asm__ __volatile__("hlt");
    }
}
