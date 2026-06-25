#pragma once
#include "span.h"
#include <stdnoreturn.h>

void error_init(const char *filename, const char *src);

void  error_at(Span span, const char *fmt, ...);
noreturn void fatal_at(Span span, const char *fmt, ...);
noreturn void fatal(const char *fmt, ...);
