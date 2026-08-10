#ifdef __3DS__
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../include/pal.h"
#include "../../include/forth.h"

static PrintConsole top_console;
static PrintConsole bottom_console;
static u16 g_draw_color565 = 0xFFFF;

static void _3ds_emit_char(char c)
{
    putchar(c);
}
static char _3ds_key_char(void)
{
    return 0;
}
static bool _3ds_key_available(void)
{
    return false;
}
static void _3ds_print_str(const char *str)
{
    fputs(str, stdout);
}
static void *_3ds_alloc_mem(size_t bytes)
{
    return malloc(bytes);
}
static void _3ds_free_mem(void *ptr)
{
    free(ptr);
}
static void _3ds_yield(void)
{
    gspWaitForVBlank();
}
static void _3ds_flush(void)
{
    gfxFlushBuffers();
    gfxSwapBuffers();
}

static void _3ds_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    g_draw_color565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static void _3ds_draw_pixel(int x, int y)
{
    if (x < 0 || x >= 400 || y < 0 || y >= 240)
    {
        return;
    }
    u16 fb_w, fb_h;

    u16 *fb_left = (u16 *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fb_w, &fb_h);
    u16 *fb_right = (u16 *)gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, &fb_w, &fb_h);

    u32 index = (x * 240) + (239 - y);

    fb_left[index] = g_draw_color565;
    if (fb_right)
    {
        fb_right[index] = g_draw_color565;
    }
}

static void _3ds_draw_rect(int x, int y, int w, int h)
{
    if (x < 0)
    {
        w += x;
        x = 0;
    }
    if (y < 0)
    {
        h += y;
        y = 0;
    }
    if (x >= 400 || y >= 240 || w <= 0 || h <= 0)
    {
        return;
    }
    if (x + w > 400)
    {
        w = 400 - x;
    }
    if (y + h > 240)
    {
        h = 240 - y;
    }

    u16 fb_w, fb_h;
    u16 *fb_left = (u16 *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fb_w, &fb_h);
    u16 *fb_right = (u16 *)gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, &fb_w, &fb_h);

    for (int iy = y; iy < y + h; iy++)
    {
        u32 row = 239 - iy;
        u32 base = x * 240 + row;
        u16 *lp = fb_left + base;
        u16 *rp = fb_right ? fb_right + base : NULL;
        for (int ix = 0; ix < w; ix++)
        {
            *lp = g_draw_color565;
            lp += 240;
            if (rp)
            {
                *rp = g_draw_color565;
                rp += 240;
            }
        }
    }
}

static void _3ds_clear_gfx(void)
{
    u16 fb_w, fb_h;
    u16 *fb_left = (u16 *)gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fb_w, &fb_h);
    u16 *fb_right = (u16 *)gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, &fb_w, &fb_h);

    memset(fb_left, 0, fb_w * fb_h * sizeof(u16));
    if (fb_right)
    {
        memset(fb_right, 0, fb_w * fb_h * sizeof(u16));
    }
}

static void _3ds_flush_gfx(void)
{
    gfxFlushBuffers();
    gspWaitForVBlank();
}

PAL_System g_pal = {.emit_char = _3ds_emit_char,
                    .key_char = _3ds_key_char,
                    .key_available = _3ds_key_available,
                    .print_str = _3ds_print_str,
                    .alloc_mem = _3ds_alloc_mem,
                    .free_mem = _3ds_free_mem,
                    .yield = _3ds_yield,
                    .flush = _3ds_flush,
                    .set_color = _3ds_set_color,
                    .draw_pixel = _3ds_draw_pixel,
                    .draw_rect = _3ds_draw_rect,
                    .clear_gfx = _3ds_clear_gfx,
                    .flush_gfx = _3ds_flush_gfx};

void target_3ds_init(void)
{
    gfxInitDefault();
    gfxSet3D(true);

    gfxSetDoubleBuffering(GFX_TOP, false);
    gfxSetDoubleBuffering(GFX_BOTTOM, false);

    consoleInit(GFX_TOP, &top_console);
    consoleInit(GFX_BOTTOM, &bottom_console);
    consoleSelect(&top_console);

    gfxSetScreenFormat(GFX_TOP, GSP_RGB565_OES);
}

void target_3ds_deinit(void)
{
    gfxExit();
}

void target_3ds_update_ui(ForthEngine *vm)
{
    consoleSelect(&bottom_console);
    consoleClear();

    printf(ANSI_CYAN "======================================\n" ANSI_RESET);
    printf(ANSI_YELLOW "     C23 FORTH 3DS ENGINE TELEMETRY   \n" ANSI_RESET);
    printf(ANSI_CYAN "======================================\n" ANSI_RESET);

    printf(" State: " ANSI_MAGENTA "%s" ANSI_RESET " | Dict: " ANSI_GREEN "%luB\n" ANSI_RESET,
           vm->state == 1 ? "COMPILING" : "INTERPRETING", (unsigned long)vm->here);

    printf(" Last Cmd: " ANSI_CYAN "%.24s" ANSI_RESET "\n",
           vm->last_command[0] != '\0' ? vm->last_command : "(none)");

    printf(" Data: " ANSI_GREEN "%d/%zu" ANSI_RESET " | Ret: " ANSI_BLUE "%d/%zu" ANSI_RESET
           " | S: %s\n",
           vm->sp + 1, STACK_CAPACITY, vm->rp + 1, STACK_CAPACITY, vm->silent ? "SILENT" : "TEXT");

    printf(ANSI_BLUE " +----------------------------------+\n" ANSI_RESET);
    printf(" Top of Stack (TOS):\n");

    if (vm->sp < 0)
    {
        printf(" | " ANSI_DARK "          (Stack Empty)           " ANSI_RESET " |\n");
    }
    else
    {
        for (int i = vm->sp; i >= 0 && i >= vm->sp - 2; i--)
        {
            if (i == vm->sp)
            {
                printf(" | " ANSI_YELLOW "-> [%3d]  %18lld" ANSI_RESET " |\n", i,
                       (long long)(scell_t)vm->data_stack[i]);
            }
            else
            {
                printf(" |    [%3d]  %18lld |\n", i, (long long)(scell_t)vm->data_stack[i]);
            }
        }
    }
    printf(ANSI_BLUE " +----------------------------------+\n" ANSI_RESET);

    printf(ANSI_WHITE " [A]/Touch" ANSI_RESET ": Keyboard | " ANSI_WHITE "[X]" ANSI_RESET
                      ": Clear\n");
    printf(ANSI_WHITE " [START]  " ANSI_RESET ": Exit Homebrew\n");

    consoleSelect(&top_console);
}

bool target_3ds_prompt(char *out_buf, size_t max_len)
{
    SwkbdState swkbd;
    swkbdInit(&swkbd, SWKBD_TYPE_NORMAL, 2, max_len - 1);
    swkbdSetHintText(&swkbd, "Enter Forth words (e.g. 255 0 0 GR-COLOR!)");
    swkbdSetButton(&swkbd, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&swkbd, SWKBD_BUTTON_RIGHT, "Eval", true);

    SwkbdButton button = swkbdInputText(&swkbd, out_buf, max_len);
    return (button == SWKBD_BUTTON_RIGHT && strlen(out_buf) > 0);
}
#endif
