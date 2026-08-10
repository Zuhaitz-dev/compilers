#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define ARENA_BLOCK_SIZE ((size_t)128 * 1024) /* 128KB per block */

typedef struct arena_block
{
    struct arena_block *next;
    size_t used;
    char data[];
} arena_block_t;

typedef struct
{
    arena_block_t *head;
    arena_block_t *current;
} arena_t;

/* Initialize an arena (empty) */
static inline void arena_init(arena_t *a)
{
    a->head = NULL;
    a->current = NULL;
}

/* Allocate from the arena (bump allocator) */
static inline void *arena_alloc(arena_t *a, size_t size)
{
    /* Align to 16 bytes */
    size = (size + 15) & ~15;

    if (!a->current || a->current->used + size > ARENA_BLOCK_SIZE)
    {
        size_t blk_sz = sizeof(arena_block_t) + ARENA_BLOCK_SIZE;
        arena_block_t *blk = (arena_block_t *)malloc(blk_sz);
        if (!blk)
        {
            return NULL;
        }
        blk->next = NULL;
        blk->used = 0;
        if (a->current)
        {
            a->current->next = blk;
        }
        else
        {
            a->head = blk;
        }
        a->current = blk;
    }

    void *ptr = a->current->data + a->current->used;
    a->current->used += size;
    return ptr;
}

/* Duplicate a string using arena memory */
static inline char *arena_strdup(arena_t *a, const char *s)
{
    size_t len = strlen(s) + 1;
    char *p = (char *)arena_alloc(a, len);
    if (p)
    {
        memcpy(p, s, len);
    }
    return p;
}

/* Free all memory allocated from the arena */
static inline void arena_free_all(arena_t *a)
{
    arena_block_t *blk = a->head;
    while (blk)
    {
        arena_block_t *next = blk->next;
        free(blk);
        blk = next;
    }
    a->head = NULL;
    a->current = NULL;
}

#endif
