#include <stdio.h>
#include <stdlib.h>
#include <string.h>

long b_print(long b_str_ptr)
{
    unsigned long long *b_string_memory = (unsigned long long *)(b_str_ptr * 8);

    char c_buffer[4096];
    int c_idx = 0;
    int word_idx = 0;
    int done = 0;

    while (!done && c_idx < 4095)
    {
        unsigned long long current_word = b_string_memory[word_idx];

        for (int i = 0; i <= 7; i++)
        {
            char c = (current_word >> (i * 8)) & 0xFF;
            if (c == '\0')
            {
                done = 1;
                break;
            }
            c_buffer[c_idx++] = c;
        }
        word_idx++;
    }

    c_buffer[c_idx] = '\0';
    printf("%s", c_buffer);

    return 0;
}

long b_getvec(long n)
{
    long physical_size = (n + 1) * 8;

    void *ptr = malloc(physical_size);
    if (!ptr)
    {
        printf("FATAL: getvec failed to allocate %ld words.\n", n);
        exit(1);
    }

    memset(ptr, 0, physical_size);

    return (long)ptr / 8;
}

long b_putchar(long c)
{
    putchar((int)c);
    return c;
}

long b_char(long b_str_addr, long index)
{
    char *str = (char *)(b_str_addr * 8);
    return (long)str[index];
}

long b_rlsevec(long b_addr, long n)
{
    (void)n;
    void *physical_ptr = (void *)(b_addr * 8);
    free(physical_ptr);
    return 0;
}

long b_fopen(long b_filename_addr, long b_mode_addr)
{
    char *filename = (char *)(b_filename_addr * 8);
    char *mode = (char *)(b_mode_addr * 8);

    FILE *f = fopen(filename, mode);

    return (long)f;
}

long b_getc(long b_file_handle)
{
    FILE *f = (FILE *)b_file_handle;
    if (!f)
    {
        return -1;
    }

    int c = fgetc(f);
    return (long)c;
}

long b_putc(long c, long b_file_handle)
{
    FILE *f = (FILE *)b_file_handle;
    if (!f)
    {
        return -1;
    }

    fputc((int)c, f);
    return c;
}

long b_fclose(long b_file_handle)
{
    FILE *f = (FILE *)b_file_handle;
    if (f)
    {
        fclose(f);
    }
    return 0;
}

long b_init_argv(long argc, char **argv)
{
    long b_argv = b_getvec(argc);
    long *physical_b_argv = (long *)(b_argv * 8);

    for (long i = 0; i < argc; i++)
    {
        long len = strlen(argv[i]);
        long words_needed = (len / 8) + 1;

        long b_str_addr = b_getvec(words_needed);
        char *physical_str = (char *)(b_str_addr * 8);

        strcpy(physical_str, argv[i]);

        physical_b_argv[i] = b_str_addr;
    }

    return b_argv;
}
