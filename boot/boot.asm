; ============================================================================
; AI_OS Bootloader (512-byte MBR)
; Loads kernel, sets up GDT, enters 32-bit protected mode, jumps to kernel
; ============================================================================

[BITS 16]
[ORG 0x7C00]

KERNEL_LOAD_SEG   equ 0x1000       ; load at 0x1000:0 = physical 0x10000
KERNEL_SECTORS    equ 128          ; 128 sectors = 64KB max kernel
KERNEL_OFFSET     equ 0x10000

; --- 16-bit entry ---
start:
    cli
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0x7C00
    sti
    mov  [boot_drive], dl

    ; Load kernel sectors from disk (two reads for safety)
    ; Read first 64 sectors
    mov  bx, KERNEL_LOAD_SEG
    mov  es, bx
    xor  bx, bx
    mov  ah, 0x02
    mov  al, 64
    mov  ch, 0
    mov  cl, 2              ; start at sector 2
    mov  dh, 0
    mov  dl, [boot_drive]
    int  0x13
    jc   .err

    ; Read next 64 sectors to 0x1000:0x8000 = physical 0x18000
    mov  bx, 0x8000
    mov  ah, 0x02
    mov  al, 64
    mov  ch, 0
    mov  cl, 66             ; sector 66
    mov  dh, 0
    mov  dl, [boot_drive]
    int  0x13
    ; If second read fails, continue anyway (kernel might be small)

    ; Enable A20
    mov  ax, 0x2401
    int  0x15
    in   al, 0x92
    or   al, 2
    out  0x92, al

    ; Enter protected mode
    cli
    lgdt [gdt_desc]
    mov  eax, cr0
    or   eax, 1
    mov  cr0, eax
    jmp  0x08:pm_entry

.err:
    mov  ah, 0x0E
    mov  al, '!'
    int  0x10
    cli
    hlt

; --- 32-bit protected mode entry (still inside boot sector) ---
[BITS 32]
pm_entry:
    mov  ax, 0x10           ; data segment selector
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  esp, 0x90000
    jmp  KERNEL_OFFSET      ; jump to kernel!

; --- GDT ---
[BITS 16]
gdt_start:
    dq 0
    ; code segment
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
    ; data segment
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

boot_drive: db 0

; --- Pad to 510 bytes + boot signature ---
times 510 - ($ - $$) db 0
dw 0xAA55
