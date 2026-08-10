#ifndef VPARSER_H
#define VPARSER_H

#include "netlist.h"

#define MAX_SIGNALS 256
#define MAX_ASGN 256
#define MAX_FF 128

typedef struct
{
    char name[64];
    int is_input;
    int is_output;
} vsignal_t;

typedef struct
{
    char dst[64];
    char op[16];
    char src1[64];
    char src2[64];
} vasgn_t;

typedef struct
{
    char name[64];
    char type[16]; /* DFF or DLATCH */
    char d[64];
    char clk[64];
} vff_t;

typedef struct
{
    char name[64];
    vsignal_t signals[MAX_SIGNALS];
    int num_signals;
    vasgn_t assigns[MAX_ASGN];
    int num_assigns;
    vff_t ffs[MAX_FF];
    int num_ffs;
} vmodule_t;

/* Parse a Verilog file into a vmodule_t structure */
int vparse(const char *filename, vmodule_t *mod);

/* Build a netlist from a parsed vmodule_t */
netlist_t *vbuild(const vmodule_t *mod);

#endif
