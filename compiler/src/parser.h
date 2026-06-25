#pragma once
#include "ast.h"
#include "lexer.h"
#include "arena.h"

Module *parse(const char *src, uint32_t file_id, Arena *arena);
