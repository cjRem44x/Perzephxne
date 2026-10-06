#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

static const char *g_filename;
static const char *g_src;
static int         g_had_error;

typedef struct { const char *name; const char *src; } SrcFile;
static SrcFile  *g_files;
static uint32_t  g_n_files, g_cap_files;

uint32_t error_register_file(const char *filename, const char *src) {
    if (g_n_files == g_cap_files) {
        g_cap_files = g_cap_files ? g_cap_files * 2 : 16;
        g_files = realloc(g_files, g_cap_files * sizeof(SrcFile));
        if (!g_files) { perror("realloc"); exit(1); }
    }
    size_t n = strlen(filename) + 1;
    char *name = malloc(n);
    if (!name) { perror("malloc"); exit(1); }
    memcpy(name, filename, n);
    g_files[g_n_files++] = (SrcFile){ name, src };
    return g_n_files; /* ids start at 1; 0 means "current error_init file" */
}

void error_init(const char *filename, const char *src) {
    g_filename  = filename;
    g_src       = src;
    g_had_error = 0;
}

int error_had(void) { return g_had_error; }

static void print_line(const char *src, uint32_t line) {
    if (!src) return;
    const char *p = src;
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
    const char *name = g_filename, *src = g_src;
    if (span.file_id >= 1 && span.file_id <= g_n_files) {
        name = g_files[span.file_id - 1].name;
        src  = g_files[span.file_id - 1].src;
    }
    fprintf(stderr, "%s:%u:%u: %s: ", name, span.start.line, span.start.col, level);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    print_line(src, span.start.line);
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
