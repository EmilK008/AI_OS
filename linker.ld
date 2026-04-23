/* Linker script for AI_OS kernel (PE target, will be objcopy'd to binary)
 *
 * Memory layout:
 *   0x00010000 .. 0x0009FFFF : kernel image (.text + .rodata + .data)
 *   0x000A0000 .. 0x000BFFFF : VGA framebuffer hole (HARDWARE, not RAM)
 *   0x000C0000 .. 0x000FFFFF : BIOS / option-ROM shadow (TYPICALLY READ-ONLY)
 *   0x00100000 .. 0x001FFFFF : .bss lives here (plain extended RAM)
 *   0x001F0000               : kernel stack top (grows down) - wait, overlaps!
 *
 * Note: .bss is placed at 1 MB, NOT directly after .data. Earlier versions
 * of this script put .bss right after .data and, once BearSSL was added,
 * a ~300 KB .bss crossed the 0xA0000 and 0xC0000 boundaries. Static vars
 * that landed in 0xC0000..0xFFFFF were silently unwritable (ROM shadow),
 * causing bizarre "global bool can't be set to true" bugs.
 *
 * .bss is NOLOAD so the gap between .data and 0x100000 does NOT bloat the
 * binary - objcopy -O binary skips NOLOAD sections.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    .text ALIGN(16) : {
        *(.text)
        *(.rodata*)
        *(.rdata*)
    }

    .data ALIGN(16) : {
        *(.data)
    }

    /* Hard cap: make sure .text + .data fit below the VGA hole. */
    _kernel_image_end = .;
    ASSERT(_kernel_image_end <= 0xA0000,
           "Kernel image (.text+.data) must fit below 0xA0000 (VGA hole)")

    /* Jump to 1 MB before placing .bss, so it lives in extended RAM
     * rather than getting stomped by VGA memory or BIOS ROM shadow. */
    . = 0x00100000;
    _bss_start = .;
    .bss ALIGN(16) (NOLOAD) : {
        *(.bss)
        *(COMMON)
    }
    _bss_end = .;

    /* Hard cap: .bss must end before the kernel stack (0x1F0000) and
     * before the heap (0x200000). */
    ASSERT(_bss_end <= 0x1F0000,
           "BSS overflows into kernel stack / heap (grew past 0x1F0000)")

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.reloc)
    }
}
