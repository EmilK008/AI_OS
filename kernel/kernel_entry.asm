; ============================================================================
; Kernel Entry Point (assembly stub)
; Sets up the stack and calls the C kernel_main()
; MinGW PE convention: C symbols have leading underscore
; ============================================================================
[BITS 32]
[GLOBAL _start]
[EXTERN _kernel_main]

section .text

[EXTERN _bss_start]
[EXTERN _bss_end]

_start:
    mov  esp, 0x1F0000
    cld

    ; Zero out BSS section
    mov  edi, _bss_start
    mov  ecx, _bss_end
    sub  ecx, edi
    shr  ecx, 2          ; byte count -> dword count
    xor  eax, eax
    rep  stosd

    call _kernel_main
.hang:
    cli
    hlt
    jmp .hang

; ============================================================================
; Exception ISR stubs (CPU exceptions 0-31)
; ============================================================================
[EXTERN _exception_handler]

%macro ISR_NOERRCODE 1
[GLOBAL _isr_stub_exc_%1]
_isr_stub_exc_%1:
    push dword 0          ; dummy error code
    push dword %1         ; interrupt number
    jmp  _exception_common
%endmacro

%macro ISR_ERRCODE 1
[GLOBAL _isr_stub_exc_%1]
_isr_stub_exc_%1:
    push dword %1         ; interrupt number (error code already on stack)
    jmp  _exception_common
%endmacro

_exception_common:
    pushad
    push esp              ; pointer to exception frame
    call _exception_handler
    add  esp, 4           ; pop argument
    popad
    add  esp, 8           ; pop error code + interrupt number
    iretd

; Generate exception stubs
ISR_NOERRCODE 0    ; Divide by zero
ISR_NOERRCODE 1    ; Debug
ISR_NOERRCODE 2    ; NMI
ISR_NOERRCODE 3    ; Breakpoint
ISR_NOERRCODE 4    ; Overflow
ISR_NOERRCODE 5    ; Bound range exceeded
ISR_NOERRCODE 6    ; Invalid opcode
ISR_NOERRCODE 7    ; Device not available
ISR_ERRCODE   8    ; Double fault
ISR_NOERRCODE 9    ; Coprocessor segment overrun
ISR_ERRCODE   10   ; Invalid TSS
ISR_ERRCODE   11   ; Segment not present
ISR_ERRCODE   12   ; Stack-segment fault
ISR_ERRCODE   13   ; General protection fault
ISR_ERRCODE   14   ; Page fault
ISR_NOERRCODE 15   ; Reserved
ISR_NOERRCODE 16   ; x87 FP exception
ISR_ERRCODE   17   ; Alignment check
ISR_NOERRCODE 18   ; Machine check
ISR_NOERRCODE 19   ; SIMD FP exception

; ============================================================================
; Keyboard ISR stub
; ============================================================================
[GLOBAL _isr_stub_keyboard]
[EXTERN _keyboard_handler]

_isr_stub_keyboard:
    pushad
    call _keyboard_handler
    popad
    iretd

; ============================================================================
; Timer ISR stub - calls scheduler for context switching
; ============================================================================
[GLOBAL _isr_stub_timer]
[EXTERN _scheduler_tick]

_isr_stub_timer:
    pushad                    ; Save all GPRs (32 bytes: EDI..EAX)
    push esp                  ; Pass current ESP as argument
    call _scheduler_tick      ; Returns new ESP in EAX
    mov  esp, eax             ; Context switch: point to new process stack
    popad                     ; Restore new process GPRs
    iretd                     ; Resume new process (pops EIP, CS, EFLAGS)

; ============================================================================
; IDT loader
; ============================================================================
[GLOBAL _idt_load]
_idt_load:
    mov  eax, [esp + 4]
    lidt [eax]
    ret

; ============================================================================
; I/O port helpers
; ============================================================================
[GLOBAL _outb]
_outb:
    mov  al, [esp + 8]
    mov  dx, [esp + 4]
    out  dx, al
    ret

[GLOBAL _inb]
_inb:
    xor  eax, eax
    mov  dx, [esp + 4]
    in   al, dx
    ret

[GLOBAL _outw]
_outw:
    mov  ax, [esp + 8]
    mov  dx, [esp + 4]
    out  dx, ax
    ret

[GLOBAL _inw]
_inw:
    xor  eax, eax
    mov  dx, [esp + 4]
    in   ax, dx
    ret

[GLOBAL _outl]
_outl:
    mov  eax, [esp + 8]
    mov  dx, [esp + 4]
    out  dx, eax
    ret

[GLOBAL _inl]
_inl:
    mov  dx, [esp + 4]
    in   eax, dx
    ret

; ============================================================================
; Mouse ISR stub (IRQ12 = vector 44)
; ============================================================================
[GLOBAL _isr_stub_mouse]
[EXTERN _mouse_handler]

_isr_stub_mouse:
    pushad
    call _mouse_handler
    popad
    iretd
