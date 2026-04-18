/* ===========================================================================
 * IDT - Interrupt Descriptor Table
 * CPU exception handlers + hardware interrupt ISRs
 * =========================================================================== */
#include "idt.h"
#include "io.h"
#include "vga.h"
#include "string.h"

#define IDT_ENTRIES 256

struct idt_entry {
    uint16_t base_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  flags;
    uint16_t base_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* Exception frame pushed by our ISR stubs */
struct exception_frame {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; /* pushad */
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
};

static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr   idtp;

/* Assembly functions */
extern void idt_load(struct idt_ptr *ptr);
extern void isr_stub_keyboard(void);
extern void isr_stub_timer(void);
extern void isr_stub_mouse(void);

/* Exception ISR stubs (from kernel_entry.asm) */
extern void isr_stub_exc_0(void);
extern void isr_stub_exc_1(void);
extern void isr_stub_exc_2(void);
extern void isr_stub_exc_3(void);
extern void isr_stub_exc_4(void);
extern void isr_stub_exc_5(void);
extern void isr_stub_exc_6(void);
extern void isr_stub_exc_7(void);
extern void isr_stub_exc_8(void);
extern void isr_stub_exc_9(void);
extern void isr_stub_exc_10(void);
extern void isr_stub_exc_11(void);
extern void isr_stub_exc_12(void);
extern void isr_stub_exc_13(void);
extern void isr_stub_exc_14(void);
extern void isr_stub_exc_15(void);
extern void isr_stub_exc_16(void);
extern void isr_stub_exc_17(void);
extern void isr_stub_exc_18(void);
extern void isr_stub_exc_19(void);

/* Debug output via QEMU debug port 0xE9 */
static void exc_dbg_putc(char c) {
    __asm__ __volatile__("outb %0, %1" : : "a"((uint8_t)c), "Nd"((uint16_t)0xE9));
}
static void exc_dbg_print(const char *s) {
    while (*s) exc_dbg_putc(*s++);
}
static char hex_digit(uint8_t v) {
    v &= 0xF;
    return (v < 10) ? (char)('0' + v) : (char)('A' + v - 10);
}
static void exc_dbg_hex(uint32_t n) {
    exc_dbg_putc('0');
    exc_dbg_putc('x');
    for (int i = 28; i >= 0; i -= 4)
        exc_dbg_putc(hex_digit((uint8_t)(n >> i)));
}

/* C exception handler called from assembly */
void exception_handler(struct exception_frame *frame) {
    /* Debug (#1) and breakpoint (#3) are non-fatal — just return */
    if (frame->int_no == 1 || frame->int_no == 3) return;

    /* Fatal exception: minimal debug output then hard freeze */
    exc_dbg_print("\n!EXC ");
    /* Print int_no as 2 decimal digits */
    exc_dbg_putc('0' + (char)(frame->int_no / 10));
    exc_dbg_putc('0' + (char)(frame->int_no % 10));
    exc_dbg_print(" e=");
    exc_dbg_hex(frame->err_code);
    exc_dbg_print(" @");
    exc_dbg_hex(frame->eip);
    exc_dbg_print("\n");

    /* Hard freeze — infinite loop, NOT hlt (NMI can wake hlt) */
    __asm__ __volatile__("cli");
    for (;;) __asm__ __volatile__("hlt");
}

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].selector  = sel;
    idt[num].zero      = 0;
    idt[num].flags     = flags;
}

/* Remap PIC to IRQ 32-47 (away from CPU exceptions 0-31) */
static void pic_remap(void) {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);
    outb(0xA1, 0x28);
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    outb(0x21, 0xF8);  /* Master: unmask IRQ0, IRQ1, IRQ2 (cascade) */
    outb(0xA1, 0xEF);  /* Slave: unmask IRQ12 (mouse) */
}

void idt_init(void) {
    mem_set(idt, 0, sizeof(idt));

    pic_remap();

    /* CPU exception handlers (0-19) */
    idt_set_gate(0,  (uint32_t)isr_stub_exc_0,  0x08, 0x8E);
    idt_set_gate(1,  (uint32_t)isr_stub_exc_1,  0x08, 0x8E);
    idt_set_gate(2,  (uint32_t)isr_stub_exc_2,  0x08, 0x8E);
    idt_set_gate(3,  (uint32_t)isr_stub_exc_3,  0x08, 0x8E);
    idt_set_gate(4,  (uint32_t)isr_stub_exc_4,  0x08, 0x8E);
    idt_set_gate(5,  (uint32_t)isr_stub_exc_5,  0x08, 0x8E);
    idt_set_gate(6,  (uint32_t)isr_stub_exc_6,  0x08, 0x8E);
    idt_set_gate(7,  (uint32_t)isr_stub_exc_7,  0x08, 0x8E);
    idt_set_gate(8,  (uint32_t)isr_stub_exc_8,  0x08, 0x8E);
    idt_set_gate(9,  (uint32_t)isr_stub_exc_9,  0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr_stub_exc_10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr_stub_exc_11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr_stub_exc_12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr_stub_exc_13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr_stub_exc_14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr_stub_exc_15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr_stub_exc_16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr_stub_exc_17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr_stub_exc_18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr_stub_exc_19, 0x08, 0x8E);

    /* Hardware interrupts */
    idt_set_gate(32, (uint32_t)isr_stub_timer,    0x08, 0x8E);
    idt_set_gate(33, (uint32_t)isr_stub_keyboard,  0x08, 0x8E);
    idt_set_gate(44, (uint32_t)isr_stub_mouse,     0x08, 0x8E);

    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint32_t)&idt;
    idt_load(&idtp);

    /* NOTE: Interrupts are NOT enabled here.
     * The caller must issue 'sti' after all subsystems
     * (especially process_init) are ready for IRQs. */
}
