#ifndef MEMORY_H
#define MEMORY_H

#include "types.h"

void     memory_init(void);
void    *kmalloc(uint32_t size);
void     kfree(void *ptr);
uint32_t memory_get_free(void);
uint32_t memory_get_total(void);
uint32_t memory_get_heap_start(void);
uint32_t memory_get_heap_end(void);

#endif
