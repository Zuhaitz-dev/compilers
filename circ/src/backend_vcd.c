#include "backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

void backend_vcd(netlist_t *nl, int argc, char **argv)
{
    /* Parse cycles */
    int cycles = 1;
    char clock_port[NAME_MAX] = "CLK";

    for (int i = 0; i < argc; i++)
    {
        if (!argv || !argv[i])
        {
            continue;
        }
        if (sscanf(argv[i], "--cycles=%d", &cycles) == 1)
        {
            continue;
        }
        if (sscanf(argv[i], "--clock=%63s", clock_port) == 1)
        {
            continue;
        }
        char name[NAME_MAX];
        int val;
        if (sscanf(argv[i], "%63[^=]=%d", name, &val) == 2)
        {
            node_t *n = netlist_find_node(nl, name);
            if (n && n->type == NODE_INPUT)
            {
                n->value = val & ((n->width >= 31) ? 0x7FFFFFFF : (1 << n->width) - 1);
            }
        }
    }

    /* Find clock */
    node_t *clock_node = NULL;
    for (int i = 0; i < nl->num_nodes; i++)
    {
        if (nl->nodes[i]->type == NODE_INPUT && strcasecmp(nl->nodes[i]->name, clock_port) == 0)
        {
            clock_node = nl->nodes[i];
            break;
        }
    }
    if (!clock_node)
    {
        for (int i = 0; i < nl->num_nodes; i++)
        {
            if (nl->nodes[i]->type == NODE_INPUT && strcasecmp(nl->nodes[i]->name, "CLK") == 0)
            {
                clock_node = nl->nodes[i];
                break;
            }
        }
    }

    int clk_started = 0;
    if (clock_node)
    {
        for (int i = 0; i < argc; i++)
        {
            if (argv && argv[i] && strstr(argv[i], clock_port) && strchr(argv[i], '='))
            {
                clk_started = 1;
            }
        }
        if (!clk_started)
        {
            clock_node->value = 0;
        }
    }

    /* VCD header */
    printf("$timescale 1ns $end\n");
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        if (n->type == NODE_OUTPUT)
        {
            printf("$var wire 1 %s %s $end\n", n->name, n->name);
        }
    }
    if (clock_node)
    {
        printf("$var wire 1 CLK CLK $end\n");
    }
    printf("$enddefinitions $end\n");
    printf("$dumpvars\n");
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        if (n->type == NODE_OUTPUT)
        {
            printf("b%d %s\n", n->value, n->name);
        }
    }
    if (clock_node)
    {
        printf("b%d CLK\n", clock_node->value);
    }
    printf("$end\n");

    /* Init eval */
    int *seen = calloc(nl->num_nodes, sizeof(int));
    eval_settle(nl, seen);

    /* Cycles */
    for (int cyc = 0; cyc < cycles; cyc++)
    {
        if (clock_node && (cyc > 0 || !clk_started))
        {
            clock_node->value = !clock_node->value;
        }

        eval_settle(nl, seen);

        printf("#%d\n", (cyc + 1) * 10);
        for (int i = 0; i < nl->num_nodes; i++)
        {
            node_t *n = nl->nodes[i];
            if (n->type == NODE_OUTPUT)
            {
                printf("b%d %s\n", n->value, n->name);
            }
        }
        if (clock_node)
        {
            printf("b%d CLK\n", clock_node->value);
        }
    }
    free(seen);
}
