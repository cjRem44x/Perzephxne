#include "arena.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE (4096 * 16)

static ArenaBlock *new_block(size_t min_cap) {
    size_t cap = min_cap > BLOCK_SIZE ? min_cap : BLOCK_SIZE;
    ArenaBlock *b = malloc(sizeof(ArenaBlock) + cap);
    if (!b) { perror("arena malloc"); exit(1); }
    b->next = NULL;
    b->cap  = cap;
    b->used = 0;
    return b;
}

void arena_init(Arena *a) {
    a->head = new_block(BLOCK_SIZE);
}

void *arena_alloc(Arena *a, size_t size) {
    size_t align = 8;
    size = (size + align - 1) & ~(align - 1);

    if (a->head->used + size > a->head->cap) {
        ArenaBlock *b = new_block(size);
        b->next = a->head;
        a->head = b;
    }
    void *ptr = a->head->data + a->head->used;
    a->head->used += size;
    return ptr;
}

void *arena_alloc_zero(Arena *a, size_t size) {
    void *ptr = arena_alloc(a, size);
    memset(ptr, 0, size);
    return ptr;
}

char *arena_strdup(Arena *a, const char *s) {
    size_t len = strlen(s) + 1;
    char  *dst = arena_alloc(a, len);
    memcpy(dst, s, len);
    return dst;
}

char *arena_strndup(Arena *a, const char *s, size_t n) {
    char *dst = arena_alloc(a, n + 1);
    memcpy(dst, s, n);
    dst[n] = '\0';
    return dst;
}

void arena_free(Arena *a) {
    ArenaBlock *b = a->head;
    while (b) {
        ArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    a->head = NULL;
}
