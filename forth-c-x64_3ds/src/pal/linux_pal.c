#include <stdio.h>
#include <stdlib.h>
#include "../../include/pal.h"

static void linux_emit_char(char c)
{
    putchar(c);
    fflush(stdout);
}

static char linux_key_char()
{
    return (char)getchar();
}

static bool linux_key_available()
{
    return true;
}

static void linux_print_str(const char *str)
{
    fputs(str, stdout);
    fflush(stdout);
}

static void *linux_alloc_mem(size_t bytes)
{
    return malloc(bytes);
}

static void linux_free_mem(void *ptr)
{
    free(ptr);
}

static void linux_yield()
{
}

static void linux_flush()
{
    fflush(stdout);
}

PAL_System g_pal = {.emit_char = linux_emit_char,
                    .key_char = linux_key_char,
                    .key_available = linux_key_available,
                    .print_str = linux_print_str,
                    .alloc_mem = linux_alloc_mem,
                    .free_mem = linux_free_mem,
                    .yield = linux_yield,
                    .flush = linux_flush};
