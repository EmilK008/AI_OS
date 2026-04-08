#ifndef PROCESS_H
#define PROCESS_H

#include "types.h"

#define MAX_PROCESSES 16
#define PROC_STACK_SIZE 4096

/* Process states */
#define PROC_UNUSED  0
#define PROC_RUNNING 1
#define PROC_READY   2
#define PROC_SLEEPING 3
#define PROC_DEAD    4

struct process {
    uint32_t pid;
    uint32_t esp;
    uint32_t *stack_base;
    char name[32];
    uint8_t state;
    uint32_t wake_tick;  /* for sleeping processes */
};

void     process_init(void);
int      process_create(const char *name, void (*entry)(void));
void     process_exit(void);
void     process_kill(uint32_t pid);
void     process_sleep(uint32_t ticks);
int      process_count(void);
struct process *process_get_table(void);
int      process_get_current(void);
uint32_t scheduler_tick(uint32_t current_esp);

#endif
