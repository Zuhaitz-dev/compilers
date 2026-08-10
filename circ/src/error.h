#ifndef ERROR_H
#define ERROR_H

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define MAX_ERROR_LEN 256

typedef struct
{
    const char *file; /* source file (may be NULL) */
    int line;
    int col;
    char msg[MAX_ERROR_LEN];
} circ_error_t;

typedef struct
{
    void *result; /* casts to netlist_t*, grid_t*, etc. */
    circ_error_t error;
    int has_error;
} circ_result_t;

/* Clear an error to "no error" state */
static inline void error_clear(circ_error_t *e)
{
    e->file = NULL;
    e->line = 0;
    e->col = 0;
    e->msg[0] = '\0';
}

/* Set an error with file/line/col info */
static inline void error_set(circ_error_t *e, const char *file, int line, int col, const char *fmt,
                             ...)
{
    va_list args;
    e->file = file;
    e->line = line;
    e->col = col;
    va_start(args, fmt);
    vsnprintf(e->msg, MAX_ERROR_LEN, fmt, args);
    va_end(args);
}

/* Convenience: set error with just a message (no location) */
static inline void error_set_msg(circ_error_t *e, const char *fmt, ...)
{
    va_list args;
    e->file = NULL;
    e->line = 0;
    e->col = 0;
    va_start(args, fmt);
    vsnprintf(e->msg, MAX_ERROR_LEN, fmt, args);
    va_end(args);
}

/* Print an error to stderr */
static inline void error_print(circ_error_t *e)
{
    if (e->file)
    {
        fprintf(stderr, "%s:%d:%d: error: %s\n", e->file, e->line, e->col, e->msg);
    }
    else if (e->msg[0])
    {
        fprintf(stderr, "error: %s\n", e->msg);
    }
}

#endif
