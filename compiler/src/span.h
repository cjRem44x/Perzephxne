#pragma once
#include <stdint.h>

typedef struct {
    uint32_t line;
    uint32_t col;
} Pos;

typedef struct {
    Pos      start;
    Pos      end;
    uint32_t file_id;
} Span;

static inline Span span_merge(Span a, Span b) {
    return (Span){ .start = a.start, .end = b.end, .file_id = a.file_id };
}
