/* Linker script for AI_OS kernel (PE target, will be objcopy'd to binary) */

ENTRY(_start)

SECTIONS
{
    . = 0x10000;

    .text ALIGN(16) : {
        *(.text)
        *(.rodata*)
    }

    .data ALIGN(16) : {
        *(.data)
    }

    _bss_start = .;
    .bss ALIGN(16) (NOLOAD) : {
        *(.bss)
        *(COMMON)
    }
    _bss_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
        *(.reloc)
    }
}
