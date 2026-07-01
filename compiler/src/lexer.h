#pragma once
#include "token.h"
#include "arena.h"

typedef struct {
    const char *src;
    const char *cur;
    Pos         pos;
    uint32_t    file_id;
    Arena      *arena;
    int         prev_was_dot; /* last token was '.' — disables float scan for t.0.1 */
} Lexer;

void  lexer_init(Lexer *l, const char *src, uint32_t file_id, Arena *arena);
Token lexer_next(Lexer *l);
