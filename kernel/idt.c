/* ===========================================================================
 * IDT - Interrupt Descriptor Table
 * CPU exception handlers + hardware interrupt ISRs
 * =========================================================================== */
#include "idt.h"
#include "io.h"
#include "vga.h"

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

static const char *exception_names[] = {
    "Divide By Zero",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack-Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 FP Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD FP Exception",
};

/* C exception handler called from assembly */
void exception_handler(struct exception_frame *frame) {
    uint8_t err_color = VGA_COLOR(VGA_WHITE, VGA_RED);
    uint8_t info_color = VGA_COLOR(VGA_YELLOW, VGA_RED);

    vga_clear();

    /* Draw red banner */
    vga_print_colored("\n", err_color);
    vga_print_colored("  ====================================================\n", err_color);
    vga_print_colored("    KERNEL PANIC - CPU EXCEPTION                      \n", err_color);
    vga_print_colored("  ====================================================\n", err_color);

    vga_print_colored("\n  Exception: ", info_color);
    if (frame->int_no < 20) {
        vga_print_colored(exception_names[frame->int_no], err_color);
    } else {
        vga_print_colored("Unknown", err_color);
    }
    vga_print_colored(" (#", info_color);
    vga_print_dec(frame->int_no);
    vga_print_colored(")\n", info_color);

    vga_print("\n  Error Code: ");
    vga_print_hex(frame->err_code);
    vga_print("\n  EIP:        ");
    vga_print_hex(frame->eip);
    vga_print("\n  CS:         ");
    vga_print_hex(frame->cs);
    vga_print("\n  EFLAGS:     ");
    vga_print_hex(frame->eflags);

    vga_print("\n\n  Registers:\n");
    vga_print("    EAX="); vga_print_hex(frame->eax);
    vga_print("  EBX="); vga_print_hex(frame->ebx);
    vga_print("\n    ECX="); vga_print_hex(frame->ecx);
    vga_print("  EDX="); vga_print_hex(frame->edx);
    vga_print("\n    ESI="); vga_print_hex(frame->esi);
    vga_print("  EDI="); vga_print_hex(frame->edi);
    vga_print("\n    EBP="); vga_print_hex(frame->ebp);
    vga_print("  ESP="); vga_print_hex(frame->esp);

    if (frame->int_no == 14) {
        /* Page fault: CR2 contains the faulting address */
        uint32_t cr2;
        __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
        vga_print("\n\n  Page Fault Address (CR2): ");
        vga_print_hex(cr2);
        if (frame->err_code & 1) vga_print(" [protection violation]");
        else vga_print(" [page not present]");
        if (frame->err_code & 2) vga_print(" [write]");
        else vga_print(" [read]");
    }

    vga_print_colored("\n\n  System halted. Press reset to reboot.\n",
                      VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK));

    __asm__ __volatile__("cli; hlt");
}

static void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].selector  = sel;
    idt[num].zero      = 0;
    idt[num].flags     = flags;
}

/* Remap PIC to IRQ 32-47 (away from CPU exceptions 0-31) */
static void pic_remap(void) {
    outb(0x20, 0x11);  io_wait();
    outb(0xA0, 0x11);  io_wait();
    outb(0x21, 0x20);  io_wait();
    outb(0xA1, 0x28);  io_wait();
    outb(0x21, 0x04);  io_wait();
    outb(0xA1, 0x02);  io_wait();
    outb(0x21, 0x01);  io_wait();
    outb(0xA1, 0x01);  io_wait();

    outb(0x21, 0xFC);  /* Master: unmask IRQ0 and IRQ1 */
    outb(0xA1, 0xFF);  /* Slave: mask all */
}

void idt_init(void) {
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

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

    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint32_t)&idt;
    idt_load(&idtp);

    __asm__ __volatile__("sti");
}
