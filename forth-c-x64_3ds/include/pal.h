#ifndef FORTH_PAL_H
#define FORTH_PAL_H

#include <stddef.h>
#include <stdint.h>

#define ANSI_RESET "\x1b[0m"
#define ANSI_BOLD "\x1b[1m"
#define ANSI_BLACK "\x1b[30m"
#define ANSI_RED "\x1b[1;31m"
#define ANSI_GREEN "\x1b[1;32m"
#define ANSI_YELLOW "\x1b[1;33m"
#define ANSI_BLUE "\x1b[1;34m"
#define ANSI_MAGENTA "\x1b[1;35m"
#define ANSI_CYAN "\x1b[1;36m"
#define ANSI_WHITE "\x1b[1;37m"
#define ANSI_DARK "\x1b[1;30m"

// The cell size auto-adapts to the target word size:
// -> 64-bit on x86_64.
// -> 32-bit on 3DS ARM11.
typedef uintptr_t cell_t;
typedef intptr_t scell_t;

typedef struct PAL_System
{
    // Basic I/O.
    void (*emit_char)(char c);
    char (*key_char)();
    bool (*key_available)();
    void (*print_str)(const char *str);

    // Dynamic Allocation.
    void *(*alloc_mem)(size_t bytes);
    void (*free_mem)(void *ptr);

    // Platform hooks.
    void (*yield)();
    void (*flush)();

    // Hardware Graphics Hooks
    void (*set_color)(uint8_t r, uint8_t g, uint8_t b);
    void (*draw_pixel)(int x, int y);
    void (*draw_rect)(int x, int y, int w, int h);
    void (*clear_gfx)(void);
    void (*flush_gfx)(void);
} PAL_System;

// Global PAL context.
extern PAL_System g_pal;

#endif // FORTH_PAL_H
