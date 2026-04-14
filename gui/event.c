/* ===========================================================================
 * GUI Event Queue
 * Lock-free ring buffer for IRQ->main loop communication
 * =========================================================================== */
#include "event.h"
#include "string.h"

#define EVENT_QUEUE_SIZE 256

static volatile struct gui_event event_queue[EVENT_QUEUE_SIZE];
static volatile int eq_head = 0;
static volatile int eq_tail = 0;

void event_init(void) {
    eq_head = 0;
    eq_tail = 0;
}

void event_push(struct gui_event *evt) {
    int next = (eq_head + 1) % EVENT_QUEUE_SIZE;
    if (next == eq_tail) return; /* queue full, drop event */
    event_queue[eq_head] = *evt;
    eq_head = next;
}

bool event_pop(struct gui_event *evt) {
    if (eq_head == eq_tail) return false;
    *evt = event_queue[eq_tail];
    eq_tail = (eq_tail + 1) % EVENT_QUEUE_SIZE;
    return true;
}
