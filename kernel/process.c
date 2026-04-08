/* ===========================================================================
 * Process Manager & Preemptive Scheduler
 * Round-robin scheduler triggered by timer IRQ
 * =========================================================================== */
#include "process.h"
#include "memory.h"
#include "string.h"
#include "io.h"
#include "vga.h"
#include "timer.h"

static struct process proc_table[MAX_PROCESSES];
static int current_proc = 0;
static int num_procs = 0;
static bool multitasking_on = false;

/* Declared in timer.c */
extern volatile uint32_t tick_count;

void process_init(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        proc_table[i].state = PROC_UNUSED;
        proc_table[i].pid = i;
    }

    /* Process 0 = shell (already running on kernel stack) */
    proc_table[0].state = PROC_RUNNING;
    proc_table[0].pid = 0;
    proc_table[0].stack_base = NULL; /* uses kernel stack */
    str_copy(proc_table[0].name, "shell");
    current_proc = 0;
    num_procs = 1;
    multitasking_on = true;
}

int process_create(const char *name, void (*entry)(void)) {
    int slot = -1;
    for (int i = 1; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_UNUSED) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -1;

    /* Allocate stack */
    uint32_t *stack = (uint32_t *)kmalloc(PROC_STACK_SIZE);
    if (!stack) return -1;
    mem_set(stack, 0, PROC_STACK_SIZE);

    proc_table[slot].stack_base = stack;
    str_copy(proc_table[slot].name, name);
    proc_table[slot].state = PROC_READY;

    /* Set up initial stack frame for iretd + popad */
    uint32_t *sp = (uint32_t *)((uint8_t *)stack + PROC_STACK_SIZE);

    /* iretd frame */
    *--sp = 0x202;              /* EFLAGS: IF=1 */
    *--sp = 0x08;               /* CS: kernel code segment */
    *--sp = (uint32_t)entry;    /* EIP: entry point */

    /* pushad frame (EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI) */
    *--sp = 0; /* EAX */
    *--sp = 0; /* ECX */
    *--sp = 0; /* EDX */
    *--sp = 0; /* EBX */
    *--sp = 0; /* ESP (ignored by popad) */
    *--sp = 0; /* EBP */
    *--sp = 0; /* ESI */
    *--sp = 0; /* EDI */

    proc_table[slot].esp = (uint32_t)sp;
    num_procs++;

    return slot;
}

void process_exit(void) {
    if (current_proc == 0) return; /* Can't kill the shell */
    proc_table[current_proc].state = PROC_DEAD;
    /* Yield to scheduler by halting - next timer tick will switch away */
    while (1) { __asm__ __volatile__("hlt"); }
}

void process_kill(uint32_t pid) {
    if (pid == 0 || pid >= MAX_PROCESSES) return;
    if (proc_table[pid].state == PROC_UNUSED) return;

    proc_table[pid].state = PROC_DEAD;

    /* Free the stack */
    if (proc_table[pid].stack_base) {
        kfree(proc_table[pid].stack_base);
        proc_table[pid].stack_base = NULL;
    }
    proc_table[pid].state = PROC_UNUSED;
    num_procs--;
}

void process_sleep(uint32_t ticks) {
    proc_table[current_proc].state = PROC_SLEEPING;
    proc_table[current_proc].wake_tick = tick_count + ticks;
    __asm__ __volatile__("hlt"); /* Will be switched out on next timer tick */
}

int process_count(void) {
    return num_procs;
}

struct process *process_get_table(void) {
    return proc_table;
}

int process_get_current(void) {
    return current_proc;
}

/* Called from timer ISR stub in assembly - returns new ESP for context switch */
uint32_t scheduler_tick(uint32_t current_esp) {
    tick_count++;

    /* Send EOI to PIC */
    outb(0x20, 0x20);

    if (!multitasking_on || num_procs <= 1) {
        return current_esp;
    }

    /* Wake up sleeping processes */
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (proc_table[i].state == PROC_SLEEPING &&
            tick_count >= proc_table[i].wake_tick) {
            proc_table[i].state = PROC_READY;
        }
    }

    /* Save current process state */
    proc_table[current_proc].esp = current_esp;
    if (proc_table[current_proc].state == PROC_RUNNING) {
        proc_table[current_proc].state = PROC_READY;
    }

    /* Round-robin: find next READY process */
    int next = current_proc;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        next = (next + 1) % MAX_PROCESSES;
        if (proc_table[next].state == PROC_READY) {
            break;
        }
    }

    /* If no ready process found, stay on current */
    if (proc_table[next].state != PROC_READY) {
        if (proc_table[current_proc].state == PROC_READY ||
            proc_table[current_proc].state == PROC_RUNNING) {
            next = current_proc;
        } else {
            /* No runnable process - just return (will hlt) */
            return current_esp;
        }
    }

    current_proc = next;
    proc_table[current_proc].state = PROC_RUNNING;

    return proc_table[current_proc].esp;
}
