#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backend.h"
#include "grid.h"
#include "parser.h"
#include "netlist.h"
#include "subcircuit.h"
#include "vparser.h"
#include "vlayout.h"

static void print_usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [options] --sim|--truth|--c|--v <file.circ> "
            "[inputs...]\n",
            prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -I<dir>  Add directory to subcircuit search path\n");
    fprintf(stderr, "  -o FILE  Write output to FILE instead of stdout\n");
    fprintf(stderr, "           (also: --output=FILE)\n");
    fprintf(stderr, "\nBackends:\n");
    fprintf(stderr, "  --sim    Simulate the circuit (inputs: A=1 B=0 ...)\n");
    fprintf(stderr, "  --truth  Generate truth table\n");
    fprintf(stderr, "  --c      Generate C code\n");
    fprintf(stderr, "  --c --driver  Generate standalone runnable C\n");
    fprintf(stderr, "  --v      Generate Verilog\n");
    fprintf(stderr, "  --vcd    Generate VCD waveform (for GTKWave)\n");
    fprintf(stderr, "  --check  Validate the circuit and exit\n");
    fprintf(stderr, "  --from-v <file.v>  Convert Verilog to .circ\n");
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        print_usage(argv[0]);
        return 1;
    }

    /* Scan for -o <file> / --output=<file> (anywhere) and redirect stdout. */
    const char *outfile = NULL;
    char **args = calloc((size_t)argc + 1, sizeof(char *));
    int nargs = 1;
    args[0] = argv[0]; /* program name */
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-o") == 0)
        {
            if (outfile)
            {
                fprintf(stderr, "error: multiple -o flags\n");
                free(args);
                return 1;
            }
            if (i + 1 >= argc)
            {
                fprintf(stderr, "error: -o requires a file name\n");
                free(args);
                return 1;
            }
            outfile = argv[++i];
            continue;
        }
        if (strncmp(argv[i], "--output=", 9) == 0)
        {
            if (outfile)
            {
                fprintf(stderr, "error: multiple -o flags\n");
                free(args);
                return 1;
            }
            outfile = argv[i] + 9;
            continue;
        }
        args[nargs++] = argv[i];
    }
    if (outfile)
    {
        if (!freopen(outfile, "w", stdout))
        {
            perror(outfile);
            free(args);
            return 1;
        }
    }
    argv = args;
    argc = nargs;

    int argpos = 1;
    while (argpos < argc && argv[argpos][0] == '-' && argv[argpos][1] == 'I')
    {
        subcircuit_add_search_dir(argv[argpos] + 2);
        argpos++;
    }

    if (argpos + 1 >= argc)
    {
        print_usage(argv[0]);
        free(args);
        return 1;
    }

    const char *mode = argv[argpos];
    const char *filename = argv[argpos + 1];

    /* --c --driver: emit a standalone runnable C program. */
    int c_driver = 0;
    if (strcmp(mode, "--c") == 0)
    {
        for (int i = argpos + 1; i < argc; i++)
        {
            if (strcmp(argv[i], "--driver") == 0)
            {
                c_driver = 1;
            }
        }
        if (c_driver && strcmp(filename, "--driver") == 0)
        {
            if (argpos + 2 >= argc)
            {
                fprintf(stderr, "error: --c --driver requires a file\n");
                free(args);
                return 1;
            }
            filename = argv[argpos + 2];
        }
    }
    else
    {
        for (int i = argpos + 1; i < argc; i++)
        {
            if (strcmp(argv[i], "--driver") == 0)
            {
                fprintf(stderr, "error: --driver only applies to --c\n");
                free(args);
                return 1;
            }
        }
    }

    /* --from-v consumes a Verilog file, not a .circ grid. */
    if (strcmp(mode, "--from-v") == 0)
    {
        vmodule_t mod;
        if (vparse(filename, &mod) != 0)
        {
            fprintf(stderr, "Failed to parse %s\n", filename);
            free(args);
            return 1;
        }
        netlist_t *vnl = vbuild(&mod);
        if (!vnl)
        {
            fprintf(stderr, "Failed to build netlist from %s\n", filename);
            free(args);
            return 1;
        }
        grid_t *vg = vlayout(vnl, mod.name);
        vlayout_emit(vg);
        grid_free(vg);
        netlist_free(vnl);
        free(args);
        return 0;
    }

    grid_t *g = grid_load(filename);
    if (!g)
    {
        fprintf(stderr, "Failed to load %s\n", filename);
        free(args);
        return 1;
    }

    /* Extract parent directory for subcircuit search */
    const char *last_slash = strrchr(filename, '/');
    char parent_dir[LINE_MAX] = ".";
    if (last_slash)
    {
        int len = (int)(last_slash - filename);
        if (len >= LINE_MAX)
        {
            len = LINE_MAX - 1;
        }
        strncpy(parent_dir, filename, len);
        parent_dir[len] = '\0';
    }

    circ_error_t err;
    error_clear(&err);
    netlist_t *nl = parse_circuit_er(g, parent_dir, &err);
    if (!nl)
    {
        if (err.msg[0])
        {
            error_print(&err);
        }
        else
        {
            fprintf(stderr, "Failed to parse %s\n", filename);
        }
        grid_free(g);
        free(args);
        return 1;
    }
    grid_free(g);

    int rc = 0;
    if (strcmp(mode, "--check") == 0)
    {
        printf("OK: %s\n", filename);
    }
    else if (strcmp(mode, "--sim") == 0)
    {
        int sim_argc = argc - argpos - 2;
        char **sim_argv = sim_argc > 0 ? argv + argpos + 2 : NULL;
        backend_sim(nl, sim_argc, sim_argv);
    }
    else if (strcmp(mode, "--truth") == 0)
    {
        rc = backend_truth(nl);
    }
    else if (strcmp(mode, "--c") == 0)
    {
        if (c_driver)
        {
            backend_c_driver(nl);
        }
        else
        {
            backend_c(nl);
        }
    }
    else if (strcmp(mode, "--v") == 0)
    {
        backend_v(nl);
    }
    else if (strcmp(mode, "--vcd") == 0)
    {
        int sim_argc = argc - argpos - 2;
        char **sim_argv = sim_argc > 0 ? argv + argpos + 2 : NULL;
        backend_vcd(nl, sim_argc, sim_argv);
    }
    else
    {
        fprintf(stderr, "Unknown mode: %s\n", mode);
        print_usage(argv[0]);
        netlist_free(nl);
        free(args);
        return 1;
    }

    netlist_free(nl);
    free(args);
    return rc;
}
