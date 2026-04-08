#ifndef TIMER_H
#define TIMER_H

#include "types.h"

extern volatile uint32_t tick_count;

void     timer_init(void);
uint32_t timer_get_ticks(void);
void     timer_wait(uint32_t ticks);

#endif
