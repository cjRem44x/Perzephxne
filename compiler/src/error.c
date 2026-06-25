#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

static const char *g_filename;
static const char *g_src;
static int         g_had_error;

void error_init(const char *filename, const char *src) {
    g_filename  = filename;
    g_src       = src;
    g_had_error = 0;
}

int error_had(void) { return g_had_error; }

static void print_line(uint32_t line) {
    if (!g_src) return;
    const char *p = g_src;
    uint32_t l = 1;
    while (*p && l < line) {
        if (*p++ == '\n') l++;
    }
    const char *end = p;
    while (*end && *end != '\n') end++;
    fprintf(stderr, "  %.*s\n", (int)(end - p), p);
}

static void print_caret(uint32_t col) {
    fprintf(stderr, "  ");
    for (uint32_t i = 1; i < col; i++) fputc(' ', stderr);
    fprintf(stderr, "^\n");
}

static void vreport(Span span, const char *level, const char *fmt, va_list ap) {
    fprintf(stderr, "%s:%u:%u: %s: ", g_filename, span.start.line, span.start.col, level);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    print_line(span.start.line);
    print_caret(span.start.col);
}

void error_at(Span span, const char *fmt, ...) {
    g_had_error = 1;
    va_list ap;
    va_start(ap, fmt);
    vreport(span, "error", fmt, ap);
    va_end(ap);
}

noreturn void fatal_at(Span span, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vreport(span, "error", fmt, ap);
    va_end(ap);
    exit(1);
}

noreturn void fatal(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(1);
}
