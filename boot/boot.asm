; ============================================================================
; AI_OS Bootloader (512-byte MBR)
; Loads kernel via CHS disk reads, enters 32-bit protected mode
; Graphics mode is set by the kernel using Bochs VGA adapter ports
; ============================================================================

[BITS 16]
[ORG 0x7C00]

KERNEL_OFFSET     equ 0x10000
KERNEL_SECTORS    equ 255

start:
    cli
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0x7C00
    sti
    mov  [boot_drive], dl

    ; ---- Load kernel using CHS reads ----
    ; Floppy geometry: 18 sectors/track, 2 heads
    ; Read sectors LBA 1..255 to physical 0x10000+

    mov  word [cur_lba], 1
    mov  word [sectors_left], KERNEL_SECTORS
    mov  word [load_seg], 0x1000

.read_loop:
    cmp  word [sectors_left], 0
    je   .read_done

    ; Convert LBA to CHS
    mov  ax, [cur_lba]
    xor  dx, dx
    mov  bx, 18
    div  bx              ; AX = LBA/18, DX = LBA%18
    mov  cl, dl
    inc  cl              ; CL = sector (1-based)
    xor  dx, dx
    mov  bx, 2
    div  bx              ; AX = cylinder, DX = head
    mov  ch, al          ; CH = cylinder
    mov  dh, dl          ; DH = head

    ; Sectors remaining in this track
    mov  al, 19
    sub  al, cl
    cmp  al, [sectors_left]
    jbe  .count_ok
    mov  al, [sectors_left]
.count_ok:
    ; Limit read to avoid crossing a 64KB DMA boundary
    ; Physical address = load_seg * 16
    ; Remaining bytes in current 64KB page = 0x10000 - (phys & 0xFFFF)
    ; Max sectors before boundary = remaining / 512
    push ax                  ; save track-limited count
    mov  ax, [load_seg]
    shl  ax, 4               ; low 16 bits of physical addr (phys & 0xFFFF)
    neg  ax                   ; 0x10000 - low16 = bytes to boundary (mod 64K)
    jz   .no_dma_limit        ; exactly at boundary start: full 64KB available
    shr  ax, 9                ; divide by 512 = max sectors before boundary
    jz   .one_sector          ; less than 512 bytes left: do 1 sector
    pop  bx                   ; retrieve track-limited count
    cmp  bl, al
    jbe  .dma_ok
    mov  bl, al               ; limit to DMA boundary
.dma_ok:
    mov  al, bl
    jmp  .do_read
.one_sector:
    pop  ax
    mov  al, 1
    jmp  .do_read
.no_dma_limit:
    pop  ax                   ; use original count
.do_read:
    mov  [cur_count], al

    ; Read sectors
    mov  ah, 0x02
    mov  dl, [boot_drive]
    mov  bx, [load_seg]
    mov  es, bx
    xor  bx, bx
    int  0x13
    jc   .read_done

    ; Advance
    xor  ah, ah
    mov  al, [cur_count]
    add  [cur_lba], ax
    sub  [sectors_left], ax
    shl  ax, 5           ; count * 32 paragraphs
    add  [load_seg], ax

    jmp  .read_loop

.read_done:
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

; --- 32-bit protected mode entry ---
[BITS 32]
pm_entry:
    mov  ax, 0x10
    mov  ds, ax
    mov  es, ax
    mov  fs, ax
    mov  gs, ax
    mov  ss, ax
    mov  esp, 0x90000
    jmp  KERNEL_OFFSET

; --- Data ---
[BITS 16]
boot_drive:   db 0
cur_lba:      dw 0
sectors_left: dw 0
load_seg:     dw 0
cur_count:    db 0

; --- GDT ---
gdt_start:
    dq 0
    dw 0xFFFF, 0x0000
    db 0x00, 10011010b, 11001111b, 0x00
    dw 0xFFFF, 0x0000
    db 0x00, 10010010b, 11001111b, 0x00
gdt_end:

gdt_desc:
    dw gdt_end - gdt_start - 1
    dd gdt_start

; --- Pad to 510 bytes + boot signature ---
times 510 - ($ - $$) db 0
dw 0xAA55
