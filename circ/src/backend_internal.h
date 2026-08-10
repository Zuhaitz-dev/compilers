#ifndef BACKEND_INTERNAL_H
#define BACKEND_INTERNAL_H

#include "netlist.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* Replace every non-alphanumeric character with '_' so the name is a valid
   C/Verilog identifier. */
static inline void sanitize(char *buf)
{
    for (char *p = buf; *p; p++)
    {
        if (!isalnum((unsigned char)*p))
        {
            *p = '_';
        }
    }
}

/* Base variable name for a node: "<name>_<id>" sanitized. */
static inline void node_base_var(node_t *n, char *buf, int size)
{
    snprintf(buf, size, "%s_%d", n->name, n->id);
    sanitize(buf);
}

static inline int is_seq_node(node_t *n)
{
    return n->type == NODE_DFF || n->type == NODE_DLATCH || n->type == NODE_JKFF;
}

/* Strip a trailing "_<digits>" suffix from an instance name to get its
   subcircuit type name. */
static inline void type_from_inst(const char *iname, char *tname, int size)
{
    strncpy(tname, iname, size - 1);
    tname[size - 1] = '\0';
    char *p = strrchr(tname, '_');
    if (p)
    {
        int all_digits = 1;
        for (char *q = p + 1; *q; q++)
        {
            if (!isdigit((unsigned char)*q))
            {
                all_digits = 0;
                break;
            }
        }
        if (all_digits)
        {
            *p = '\0';
        }
    }
}

#endif
