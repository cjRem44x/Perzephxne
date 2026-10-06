#pragma once
#include "span.h"
#include <stdnoreturn.h>

void error_init(const char *filename, const char *src);
/* Register a source file for span reporting; returns the file_id to pass to
   parse(). Spans with file_id 0 report against error_init's current file. */
uint32_t error_register_file(const char *filename, const char *src);

void  error_at(Span span, const char *fmt, ...);
noreturn void fatal_at(Span span, const char *fmt, ...);
noreturn void fatal(const char *fmt, ...);
