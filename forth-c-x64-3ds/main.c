#include <stdio.h>
#include <string.h>
#include "include/forth.h"
#include "include/pal.h"

#ifdef __3DS__
#include <3ds.h>
extern void target_3ds_init(void);
extern void target_3ds_deinit(void);
extern void target_3ds_update_ui(ForthEngine *vm);
extern bool target_3ds_prompt(char *out_buf, size_t max_len);
#endif

static ForthEngine vm;

int main(void)
{
#ifdef __3DS__
    target_3ds_init();
#endif

    forth_init(&vm);

    g_pal.print_str(ANSI_CYAN "==============================================\n" ANSI_RESET);
    g_pal.print_str(ANSI_YELLOW "         C23 FORTH COMPILER WORKBENCH         \n" ANSI_RESET);
    g_pal.print_str(ANSI_CYAN "==============================================\n\n" ANSI_RESET);
    g_pal.print_str(ANSI_WHITE "* Architecture:" ANSI_RESET " Direct-Threaded Code (32B Line)\n");
#ifdef __3DS__
    g_pal.print_str(ANSI_WHITE "* Environment: " ANSI_RESET " Nintendo 3DS XL (ARM11 libctru)\n\n");
#else
    g_pal.print_str(ANSI_WHITE "* Environment: " ANSI_RESET " Linux x86-64\n\n");
#endif

    g_pal.print_str(ANSI_GREEN "> 10 20 +" ANSI_RESET " ok\n");
    forth_eval(&vm, "10 20 +");

#ifndef __3DS__
    {
        char input_buf[512];
        while (!vm.quit && fgets(input_buf, sizeof(input_buf), stdin))
        {
            size_t len = strlen(input_buf);
            while (len > 0 && (input_buf[len - 1] == '\n' || input_buf[len - 1] == '\r'))
            {
                input_buf[--len] = '\0';
            }
            if (len == 0)
            {
                continue;
            }

            if (!vm.silent)
            {
                g_pal.print_str(ANSI_MAGENTA "> ");
                g_pal.print_str(input_buf);
                g_pal.print_str(ANSI_RESET "\n");
            }

            forth_eval(&vm, input_buf);

            if (!vm.silent && !vm.quit)
            {
                g_pal.print_str(" " ANSI_GREEN "ok" ANSI_RESET "\n");
            }
        }
    }
#endif

#ifdef __3DS__
    char input_buf[256];

    target_3ds_update_ui(&vm);

    while (aptMainLoop() && !vm.quit)
    {
        hidScanInput();
        u32 kDown = hidKeysDown();

        if ((kDown & KEY_A) || (kDown & KEY_TOUCH))
        {
            memset(input_buf, 0, sizeof(input_buf));
            if (target_3ds_prompt(input_buf, sizeof(input_buf)))
            {

                if (!vm.silent)
                {
                    g_pal.print_str(ANSI_MAGENTA "> ");
                    g_pal.print_str(input_buf);
                    g_pal.print_str(ANSI_RESET "\n");
                }

                forth_eval(&vm, input_buf);

                if (!vm.silent && !vm.quit)
                {
                    g_pal.print_str(" " ANSI_GREEN "ok" ANSI_RESET "\n");
                }

                target_3ds_update_ui(&vm);
            }
        }

        if (kDown & KEY_X)
        {
            vm.sp = -1;
            g_pal.print_str(ANSI_RED "> STACK CLEARED\n" ANSI_RESET);

            // Update UI ONLY when the stack is cleared!
            target_3ds_update_ui(&vm);
        }

        if (kDown & KEY_START)
        {
            break;
        }

        g_pal.flush();
        g_pal.yield();
    }

    target_3ds_deinit();
#endif

    return 0;
}
