#pragma once
#include "token.h"
#include "arena.h"

typedef struct {
    const char *src;
    const char *cur;
    Pos         pos;
    uint32_t    file_id;
    Arena      *arena;
} Lexer;

void  lexer_init(Lexer *l, const char *src, uint32_t file_id, Arena *arena);
Token lexer_next(Lexer *l);
