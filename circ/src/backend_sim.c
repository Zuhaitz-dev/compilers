#include "backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static int wmask(int w)
{
    return (w >= 31) ? 0x7FFFFFFF : ((1 << w) - 1);
}

/* A width-1 source feeding a bus gate's data pin is broadcast. */
static int bcast_val(node_t *gate, node_t *src, int v)
{
    if (src->width == 1 && gate->width > 1)
    {
        return v ? wmask(gate->width) : 0;
    }
    return v;
}

/* Compute a node's value from the CURRENT values of its inputs.
   No recursion and no caching: eval_settle() iterates full passes so
   ripple counters and latches settle correctly. */
void eval_node(netlist_t *nl, node_t *n)
{
    (void)nl;
    switch (n->type)
    {
    case NODE_INPUT:
        return;
    case NODE_GND:
        n->value = 0;
        return;
    case NODE_VCC:
        n->value = wmask(n->width);
        return;
    case NODE_CLOCK:
        n->value = 0;
        return;
    case NODE_AND:
        n->value = wmask(n->width);
        for (int i = 0; i < n->num_inputs; i++)
        {
            n->value &= bcast_val(n, n->inputs[i]->src, n->inputs[i]->src->value);
        }
        break;
    case NODE_OR:
        n->value = 0;
        for (int i = 0; i < n->num_inputs; i++)
        {
            n->value |= bcast_val(n, n->inputs[i]->src, n->inputs[i]->src->value);
        }
        break;
    case NODE_NOT:
        n->value =
            n->num_inputs > 0
                ? (~bcast_val(n, n->inputs[0]->src, n->inputs[0]->src->value)) & wmask(n->width)
                : 0;
        break;
    case NODE_NAND:
        n->value = wmask(n->width);
        for (int i = 0; i < n->num_inputs; i++)
        {
            n->value &= bcast_val(n, n->inputs[i]->src, n->inputs[i]->src->value);
        }
        n->value = ~n->value & wmask(n->width);
        break;
    case NODE_NOR:
        n->value = 0;
        for (int i = 0; i < n->num_inputs; i++)
        {
            n->value |= bcast_val(n, n->inputs[i]->src, n->inputs[i]->src->value);
        }
        n->value = ~n->value & wmask(n->width);
        break;
    case NODE_XOR:
        n->value = 0;
        for (int i = 0; i < n->num_inputs; i++)
        {
            n->value ^= bcast_val(n, n->inputs[i]->src, n->inputs[i]->src->value);
        }
        break;
    case NODE_XNOR:
        n->value = 0;
        for (int i = 0; i < n->num_inputs; i++)
        {
            n->value ^= bcast_val(n, n->inputs[i]->src, n->inputs[i]->src->value);
        }
        n->value = ~n->value & wmask(n->width);
        break;
    case NODE_DFF:
    case NODE_JKFF:
        n->value = n->state;
        break;
    case NODE_DLATCH:
    {
        int d_val = 0, en_val = 0, found_d = 0, found_en = 0;
        for (int _i = 0; _i < n->num_inputs; _i++)
        {
            if (n->inputs[_i]->dst_pin == 0 && !found_d)
            {
                d_val = bcast_val(n, n->inputs[_i]->src, n->inputs[_i]->src->value);
                found_d = 1;
            }
            if (n->inputs[_i]->dst_pin == 1 && !found_en)
            {
                en_val = n->inputs[_i]->src->value;
                found_en = 1;
            }
        }
        if (en_val)
        {
            n->state = d_val & wmask(n->width);
        }
        n->value = n->state;
        break;
    }
    case NODE_OUTPUT:
        if (n->num_inputs > 0)
        {
            n->value = n->inputs[0]->src->value;
        }
        break;

    case NODE_SUBCIRCUIT:
    {
        netlist_t *sub = n->sub_netlist;
        if (!sub)
        {
            break;
        }

        for (int i = 0; i < n->num_input_bindings; i++)
        {
            node_t *sp = netlist_find_port(sub, n->input_bindings[i].name);
            if (sp && n->input_bindings[i].external_node)
            {
                sp->value = n->input_bindings[i].external_node->value;
            }
        }
        for (int i = 0; i < n->num_inputs; i++)
        {
            int port_idx = 0;
            for (int si = 0; si < sub->num_nodes; si++)
            {
                if (sub->nodes[si]->type == NODE_INPUT)
                {
                    if (port_idx == n->inputs[i]->dst_pin)
                    {
                        sub->nodes[si]->value = n->inputs[i]->src->value;
                        break;
                    }
                    port_idx++;
                }
            }
        }

        eval_settle(sub);

        n->value = 0;
        for (int si = 0; si < sub->num_nodes; si++)
        {
            if (sub->nodes[si]->type == NODE_OUTPUT && sub->nodes[si]->is_public)
            {
                n->value = sub->nodes[si]->value;
                break;
            }
        }

        for (int i = 0; i < n->num_output_bindings; i++)
        {
            node_t *sp = netlist_find_port(sub, n->output_bindings[i].name);
            if (sp && n->output_bindings[i].external_node)
            {
                n->output_bindings[i].external_node->value = sp->value;
            }
        }
        break;
    }
    default:
        break;
    }

    /* Immediately propagate gate output to connected output port nodes */
    for (int i = 0; i < n->num_outputs; i++)
    {
        node_t *dst = n->outputs[i]->dst;
        if (dst->type == NODE_OUTPUT)
        {
            dst->value = n->value;
        }
    }
}

static int settle_comb(netlist_t *nl)
{
    int max_pass = nl->num_nodes + 4;
    int any = 0;
    for (int p = 0; p < max_pass; p++)
    {
        int changed = 0;
        for (int i = 0; i < nl->num_nodes; i++)
        {
            node_t *n = nl->nodes[i];
            int before = n->value;
            eval_node(nl, n);
            if (n->value != before)
            {
                changed = 1;
            }
        }
        if (!changed)
        {
            break;
        }
        any = 1;
    }
    return any;
}

static int flop_clock_rose(node_t *n)
{
    int clk_val = 0;
    int clk_pin = (n->type == NODE_JKFF) ? 2 : 1;
    for (int i = 0; i < n->num_inputs; i++)
    {
        if (n->inputs[i]->dst_pin == clk_pin)
        {
            clk_val = n->inputs[i]->src->value;
            break;
        }
    }
    if (n->prev_clock == -1)
    { /* first eval: record, no capture */
        n->prev_clock = clk_val;
        return 0;
    }
    int rose = clk_val && !n->prev_clock;
    n->prev_clock = clk_val;
    return rose;
}

static void flop_snapshot(node_t *n)
{
    int d_val = 0, j_val = 0, k_val = 0;
    int found_d = 0, found_j = 0, found_k = 0;
    for (int i = 0; i < n->num_inputs; i++)
    {
        node_t *src = n->inputs[i]->src;
        int pin = n->inputs[i]->dst_pin;
        if (n->type == NODE_JKFF)
        {
            if (pin == 0 && !found_j)
            {
                j_val = bcast_val(n, src, src->value);
                found_j = 1;
            }
            else if (pin == 1 && !found_k)
            {
                k_val = bcast_val(n, src, src->value);
                found_k = 1;
            }
        }
        else if (pin == 0 && !found_d)
        {
            d_val = bcast_val(n, src, src->value);
            found_d = 1;
        }
    }
    int m = wmask(n->width);
    if (n->type == NODE_DFF)
    {
        n->pending = d_val & m;
    }
    else
    {
        n->pending =
            ((j_val & ~k_val) | (j_val & k_val & ~n->state) | (~j_val & ~k_val & n->state)) & m;
    }
    n->pending_valid = 1;
}

/* Settle with Verilog delta semantics: each wave settles combinational
   logic, snapshots rising flops, then applies all captures together. */
void eval_settle(netlist_t *nl)
{
    int max_waves = nl->num_nodes + 8;
    for (int w = 0; w < max_waves; w++)
    {
        settle_comb(nl);
        int rose_count = 0;
        for (int i = 0; i < nl->num_nodes; i++)
        {
            node_t *n = nl->nodes[i];
            if (n->type != NODE_DFF && n->type != NODE_JKFF)
            {
                continue;
            }
            if (flop_clock_rose(n))
            {
                flop_snapshot(n);
                rose_count++;
            }
        }
        if (!rose_count)
        {
            break;
        }
        for (int i = 0; i < nl->num_nodes; i++)
        {
            node_t *n = nl->nodes[i];
            if ((n->type == NODE_DFF || n->type == NODE_JKFF) && n->pending_valid)
            {
                n->state = n->pending;
                n->pending_valid = 0;
            }
        }
    }
}

void backend_sim(netlist_t *nl, int argc, char **argv)
{
    /* Parse input values, cycles, clock port, and dump flag */
    int cycles = 1;
    int dump = 0;
    char clock_port[NAME_MAX] = "CLK";
    int has_clock = 0;
    for (int i = 0; i < argc; i++)
    {
        if (!argv || !argv[i])
        {
            continue;
        }
        if (strcmp(argv[i], "--dump") == 0)
        {
            dump = 1;
            continue;
        }
        if (strncmp(argv[i], "--cycles=", 9) == 0 && argv[i][9] != '\0')
        {
            char *end = NULL;
            long v = strtol(argv[i] + 9, &end, 10);
            if (end != argv[i] + 9)
            {
                cycles = (int)v;
            }
            continue;
        }
        if (strncmp(argv[i], "--clock=", 8) == 0)
        {
            strncpy(clock_port, argv[i] + 8, NAME_MAX - 1);
            clock_port[NAME_MAX - 1] = '\0';
            has_clock = 1;
            continue;
        }
        char *eq = strchr(argv[i], '=');
        if (eq && eq != argv[i])
        {
            size_t nlen = (size_t)(eq - argv[i]);
            if (nlen < NAME_MAX)
            {
                char name[NAME_MAX];
                memcpy(name, argv[i], nlen);
                name[nlen] = '\0';
                char *end = NULL;
                long val = strtol(eq + 1, &end, 10);
                if (end != eq + 1)
                {
                    node_t *n = netlist_find_node(nl, name);
                    if (n && n->type == NODE_INPUT)
                    {
                        n->value = (int)val & wmask(n->width);
                    }
                }
            }
        }
    }

    /* Initialize: settle to establish initial combinational state */
    eval_settle(nl);

    /* After initialization, set prev_clock = current clock value for all DFFs
       so the FALLING edge is first detected, not rising.
       (CLK starts at 0, so after init: CLK=0, prev_clock=0 → no false edge) */
    for (int i = 0; i < nl->num_nodes; i++)
    {
        if (nl->nodes[i]->type != NODE_DFF && nl->nodes[i]->type != NODE_JKFF)
        {
            continue;
        }
        int clk_pin = (nl->nodes[i]->type == NODE_JKFF) ? 2 : 1;
        int clk_val = 0;
        for (int j = 0; j < nl->nodes[i]->num_inputs; j++)
        {
            if (nl->nodes[i]->inputs[j]->dst_pin == clk_pin)
            {
                clk_val = nl->nodes[i]->inputs[j]->src->value;
                break;
            }
        }
        nl->nodes[i]->prev_clock = clk_val;
    }

    /* Find clock node */
    node_t *clock_node = NULL;
    for (int i = 0; i < nl->num_nodes; i++)
    {
        if (nl->nodes[i]->type == NODE_INPUT && strcasecmp(nl->nodes[i]->name, clock_port) == 0)
        {
            clock_node = nl->nodes[i];
            break;
        }
    }

    /* If user didn't explicitly set --clock but there's a CLK input, auto-detect */
    if (!has_clock && !clock_node)
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

    /* Determine clock behavior:
       If user explicitly set CLK=0 or CLK=1, start there.
       If user didn't set CLK at all, start at 0 and toggle every cycle. */
    int clk_started = 0;
    if (clock_node)
    {
        for (int i = 0; i < argc; i++)
        {
            if (argv && argv[i] && strstr(argv[i], clock_port) != NULL &&
                strchr(argv[i], '=') != NULL)
            {
                clk_started = 1;
            }
        }
        if (!clk_started)
        {
            clock_node->value = 0;
        }
    }

    for (int cyc = 0; cyc < cycles; cyc++)
    {
        /* Toggle clock at start of cycle 0 if not user-set, else toggle from cycle 1 */
        if (clock_node && (cyc > 0 || !clk_started))
        {
            clock_node->value = !clock_node->value;
        }

        /* Settle evaluation per cycle */
        eval_settle(nl);

        /* Dump after each cycle if requested */
        if (dump)
        {
            printf("  cycle %d:", cyc);
            for (int i = 0; i < nl->num_nodes; i++)
            {
                node_t *n = nl->nodes[i];
                if (n->type == NODE_OUTPUT && n->is_public)
                {
                    printf(" %s=%d", n->name, n->value);
                }
            }
            printf("\n");
        }
    }

    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        if (n->type == NODE_OUTPUT && n->is_public)
        {
            printf("%s=%d ", n->name, n->value);
        }
    }
    printf("\n");
}

static void print_signal_name(node_t *n)
{
    if (n->width > 1)
    {
        printf("%s[%d:%d] ", n->name, n->bus_msb, n->bus_lsb);
    }
    else
    {
        printf("%s ", n->name);
    }
}

int backend_truth(netlist_t *nl)
{
    if (netlist_has_sequential(nl))
    {
        fprintf(stderr, "error: truth table is undefined for sequential "
                        "circuits (contains DFF/JKFF/DLATCH); use --sim or "
                        "--vcd instead\n");
        return 1;
    }

    /* Collect public inputs */
    int num_in = 0;
    node_t **inputs = netlist_get_inputs(nl, &num_in);

    int total_bits = 0;
    for (int i = 0; i < num_in; i++)
    {
        total_bits += inputs[i]->width;
    }
    if (total_bits > 16)
    {
        fprintf(stderr, "Too many input bits (%d, max 16) for truth table\n", total_bits);
        free(inputs);
        return 1;
    }

    /* Collect public output ports */
    int num_out = 0;
    node_t **outputs = netlist_get_outputs(nl, &num_out);

    for (int i = 0; i < num_in; i++)
    {
        print_signal_name(inputs[i]);
    }
    printf("| ");
    for (int i = 0; i < num_out; i++)
    {
        print_signal_name(outputs[i]);
    }
    printf("\n");

    int rows = 1 << total_bits;
    for (int r = 0; r < rows; r++)
    {
        /* assign each input its bit slice of r */
        int shift = 0;
        for (int i = 0; i < num_in; i++)
        {
            int m = wmask(inputs[i]->width);
            inputs[i]->value = (r >> shift) & m;
            shift += inputs[i]->width;
        }

        for (int i = 0; i < nl->num_nodes; i++)
        {
            eval_node(nl, nl->nodes[i]);
        }

        for (int i = 0; i < num_in; i++)
        {
            printf("%d ", inputs[i]->value);
        }
        printf("| ");
        for (int i = 0; i < num_out; i++)
        {
            printf("%d ", outputs[i]->value);
        }
        printf("\n");
    }

    free(inputs);
    free(outputs);
    return 0;
}
