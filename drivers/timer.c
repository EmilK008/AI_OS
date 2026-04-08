/* ===========================================================================
 * Timer Driver (PIT - IRQ0)
 * Programmable Interval Timer at 100 Hz
 * tick_count is incremented by the scheduler in process.c
 * =========================================================================== */
#include "timer.h"
#include "io.h"

#define PIT_FREQ    1193180
#define TARGET_FREQ 100

volatile uint32_t tick_count = 0;

void timer_init(void) {
    uint16_t divisor = PIT_FREQ / TARGET_FREQ;

    /* Channel 0, lobyte/hibyte, rate generator */
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));

    tick_count = 0;
}

uint32_t timer_get_ticks(void) {
    return tick_count;
}

void timer_wait(uint32_t ticks) {
    uint32_t start = tick_count;
    while (tick_count - start < ticks) {
        __asm__ __volatile__("hlt");
    }
}
