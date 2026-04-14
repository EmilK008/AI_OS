/* ===========================================================================
 * Simple Memory Manager
 * Block-based heap allocator
 * =========================================================================== */
#include "memory.h"

#define HEAP_START 0x200000   /* 2 MB mark */
#define HEAP_SIZE  0x400000   /* 4 MB heap */
#define HEAP_END   (HEAP_START + HEAP_SIZE)
#define BLOCK_SIZE 64         /* Minimum allocation unit */

/* Block header */
struct mem_block {
    uint32_t size;            /* Size including header */
    uint32_t magic;           /* 0xDEADBEEF when allocated */
    struct mem_block *next;
};

#define MEM_MAGIC_USED 0xDEADBEEF
#define MEM_MAGIC_FREE 0xFEEDFACE

static struct mem_block *free_list = NULL;
static uint32_t total_memory = HEAP_SIZE;
static uint32_t used_memory = 0;

void memory_init(void) {
    /* Initialize entire heap as one free block */
    free_list = (struct mem_block *)HEAP_START;
    free_list->size = HEAP_SIZE;
    free_list->magic = MEM_MAGIC_FREE;
    free_list->next = NULL;
    used_memory = 0;
}

void *kmalloc(uint32_t size) {
    /* Align to BLOCK_SIZE */
    size += sizeof(struct mem_block);
    if (size < BLOCK_SIZE) size = BLOCK_SIZE;
    size = (size + BLOCK_SIZE - 1) & ~(BLOCK_SIZE - 1);

    struct mem_block *prev = NULL;
    struct mem_block *curr = free_list;

    while (curr) {
        if (curr->size >= size) {
            /* Found a fit */
            if (curr->size >= size + BLOCK_SIZE + sizeof(struct mem_block)) {
                /* Split block */
                struct mem_block *new_block = (struct mem_block *)((uint8_t *)curr + size);
                new_block->size = curr->size - size;
                new_block->magic = MEM_MAGIC_FREE;
                new_block->next = curr->next;

                curr->size = size;

                if (prev) prev->next = new_block;
                else free_list = new_block;
            } else {
                /* Use entire block */
                if (prev) prev->next = curr->next;
                else free_list = curr->next;
            }

            curr->magic = MEM_MAGIC_USED;
            curr->next = NULL;
            used_memory += curr->size;
            return (void *)((uint8_t *)curr + sizeof(struct mem_block));
        }
        prev = curr;
        curr = curr->next;
    }

    return NULL; /* Out of memory */
}

void kfree(void *ptr) {
    if (!ptr) return;

    struct mem_block *block = (struct mem_block *)((uint8_t *)ptr - sizeof(struct mem_block));
    if (block->magic != MEM_MAGIC_USED) return; /* Invalid free */

    block->magic = MEM_MAGIC_FREE;
    used_memory -= block->size;

    /* Add to free list (sorted by address for coalescing) */
    struct mem_block *prev = NULL;
    struct mem_block *curr = free_list;

    while (curr && curr < block) {
        prev = curr;
        curr = curr->next;
    }

    block->next = curr;
    if (prev) prev->next = block;
    else free_list = block;

    /* Coalesce with next block */
    if (block->next &&
        (uint8_t *)block + block->size == (uint8_t *)block->next) {
        block->size += block->next->size;
        block->next = block->next->next;
    }

    /* Coalesce with previous block */
    if (prev &&
        (uint8_t *)prev + prev->size == (uint8_t *)block) {
        prev->size += block->size;
        prev->next = block->next;
    }
}

uint32_t memory_get_free(void) {
    return total_memory - used_memory;
}

uint32_t memory_get_total(void) {
    return total_memory;
}

uint32_t memory_get_heap_start(void) {
    return HEAP_START;
}

uint32_t memory_get_heap_end(void) {
    return HEAP_END;
}
