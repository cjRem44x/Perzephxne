#pragma once
#include <stddef.h>

typedef struct ArenaBlock ArenaBlock;

struct ArenaBlock {
    ArenaBlock *next;
    size_t      cap;
    size_t      used;
    char        data[];
};

typedef struct {
    ArenaBlock *head;
} Arena;

void  arena_init(Arena *a);
void *arena_alloc(Arena *a, size_t size);
void *arena_alloc_zero(Arena *a, size_t size);
char *arena_strdup(Arena *a, const char *s);
char *arena_strndup(Arena *a, const char *s, size_t n);
void  arena_free(Arena *a);

#define ARENA_NEW(arena, T)      ((T *)arena_alloc_zero((arena), sizeof(T)))
#define ARENA_ALLOC(arena, T, n) ((T *)arena_alloc_zero((arena), sizeof(T) * (n)))
