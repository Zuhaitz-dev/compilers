#include "backend.h"
#include "backend_internal.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "circ_internal.h"

/* ---------------------------------------------------------------
   Identifier helpers
   --------------------------------------------------------------- */

/* Emit a Verilog bus range "[msb:lsb] " for width>1, nothing for scalar. */
static void print_v_range(node_t *n)
{
    if (n->width > 1)
    {
        printf("[%d:%d] ", n->bus_msb, n->bus_lsb);
    }
}

/* Value expression for a signal (resolves labels/outputs to drivers). */
static void v_read(node_t *n, char *buf, int size, netlist_t *nl, char **sub_wire_var)
{
    if (n->type == NODE_INPUT)
    {
        snprintf(buf, size, "%s", n->name);
        sanitize(buf);
        return;
    }
    if (n->type == NODE_OUTPUT)
    {
        if (n->num_inputs > 0)
        {
            v_read(n->inputs[0]->src, buf, size, nl, sub_wire_var);
            return;
        }
        for (int i = 0; i < nl->num_nodes; i++)
        {
            node_t *s = nl->nodes[i];
            if (s->type != NODE_SUBCIRCUIT)
            {
                continue;
            }
            for (int b = 0; b < s->num_output_bindings; b++)
            {
                if (s->output_bindings[b].external_node == n && sub_wire_var[s->id])
                {
                    snprintf(buf, size, "%s", sub_wire_var[s->id]);
                    return;
                }
            }
        }
        snprintf(buf, size, "%s", n->name);
        sanitize(buf);
        return;
    }
    if (n->type == NODE_SUBCIRCUIT)
    {
        if (sub_wire_var[n->id])
        {
            snprintf(buf, size, "%s", sub_wire_var[n->id]);
        }
        else
        {
            node_base_var(n, buf, size);
        }
        return;
    }
    node_base_var(n, buf, size);
}

/* Read expression for a signal, replicating a width-1 source that feeds
   a bus gate's data pin. */
static void v_read_bcast(node_t *src, char *buf, int size, netlist_t *nl, char **sub_wire_var,
                         int gate_width)
{
    char tmp[NAME_MAX];
    v_read(src, tmp, sizeof(tmp), nl, sub_wire_var);
    if (src->width == 1 && gate_width > 1)
    {
        snprintf(buf, size, "{%d{%s}}", gate_width, tmp);
    }
    else
    {
        snprintf(buf, size, "%s", tmp);
    }
}

/* ---------------------------------------------------------------
   Subcircuit type collection
   --------------------------------------------------------------- */

typedef struct
{
    netlist_t *nl;
    char tname[NAME_MAX];
} sub_type_t;

static void collect_sub_types(netlist_t *nl, sub_type_t **types, int *num, int *cap)
{
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        if (n->type != NODE_SUBCIRCUIT)
        {
            continue;
        }
        char tname[NAME_MAX];
        type_from_inst(n->name, tname, sizeof(tname));
        int found = 0;
        for (int j = 0; j < *num; j++)
        {
            if (strcmp((*types)[j].tname, tname) == 0)
            {
                found = 1;
                break;
            }
        }
        if (!found)
        {
            if (*num >= *cap)
            {
                *cap = *cap ? *cap * 2 : 16;
                sub_type_t *nt = realloc(*types, (size_t)*cap * sizeof(sub_type_t));
                if (!nt)
                {
                    return;
                }
                *types = nt;
            }
            (*types)[*num].nl = n->sub_netlist;
            strncpy((*types)[*num].tname, tname, NAME_MAX - 1);
            (*types)[*num].tname[NAME_MAX - 1] = '\0';
            (*num)++;
            collect_sub_types(n->sub_netlist, types, num, cap);
        }
    }
}

/* ---------------------------------------------------------------
   Sequential always-block emitter
   --------------------------------------------------------------- */

static void emit_always(netlist_t *nl, node_t *n, char **sub_wire_var)
{
    char d[NAME_MAX], ck[NAME_MAX], j[NAME_MAX], k[NAME_MAX];
    d[0] = ck[0] = j[0] = k[0] = '\0';
    for (int i = 0; i < n->num_inputs; i++)
    {
        node_t *src = n->inputs[i]->src;
        int pin = n->inputs[i]->dst_pin;
        if (n->type == NODE_JKFF)
        {
            switch (pin)
            {
            case 0:
                v_read_bcast(src, j, sizeof(j), nl, sub_wire_var, n->width);
                break;
            case 1:
                v_read_bcast(src, k, sizeof(k), nl, sub_wire_var, n->width);
                break;
            case 2:
                v_read(src, ck, sizeof(ck), nl, sub_wire_var);
                break;
            default:
                break;
            }
        }
        else if (pin == 0)
        {
            v_read_bcast(src, d, sizeof(d), nl, sub_wire_var, n->width);
        }
        else
        {
            v_read(src, ck, sizeof(ck), nl, sub_wire_var);
        }
    }
    char vn[NAME_MAX];
    node_base_var(n, vn, sizeof(vn));
    if (n->type == NODE_DLATCH)
    {
        printf("  always @(*) if (%s) %s = %s;\n", ck, vn, d);
    }
    else if (n->type == NODE_DFF)
    {
        printf("  always @(posedge %s) %s <= %s;\n", ck, vn, d);
    }
    else
    {
        printf("  always @(posedge %s) %s <= (%s & ~%s) | (%s & %s & ~%s) | "
               "(~%s & ~%s & %s);\n",
               ck, vn, j, k, j, k, vn, j, k, vn);
    }
}

/* ---------------------------------------------------------------
   Assign/instance emitter (combinational logic + subcircuit calls)
   --------------------------------------------------------------- */

static void emit_inst(netlist_t *nl, node_t *n, char **sub_wire_var)
{
    netlist_t *sub = n->sub_netlist;
    char tname[NAME_MAX], iname[NAME_MAX];
    type_from_inst(n->name, tname, sizeof(tname));
    snprintf(iname, sizeof(iname), "%s", n->name);
    sanitize(iname);

    printf("  %s %s (\n", tname, iname);

    /* Count connections so commas land between ports. */
    int nconn = 0;
    for (int si = 0; si < sub->num_nodes; si++)
    {
        node_t *sp = sub->nodes[si];
        if (!sp->is_public || (sp->type != NODE_INPUT && sp->type != NODE_OUTPUT))
        {
            continue;
        }
        nconn++;
    }

    int port_idx = 0;
    for (int si = 0; si < sub->num_nodes; si++)
    {
        node_t *sp = sub->nodes[si];
        if (sp->type != NODE_INPUT || !sp->is_public)
        {
            continue;
        }

        const char *port_name = sp->name;
        char buf[NAME_MAX] = "1'b0";
        int got = 0;
        for (int b = 0; b < n->num_input_bindings; b++)
        {
            if (strcmp(n->input_bindings[b].name, port_name) == 0)
            {
                v_read(n->input_bindings[b].external_node, buf, sizeof(buf), nl, sub_wire_var);
                got = 1;
                break;
            }
        }
        if (!got)
        {
            for (int j = 0; j < n->num_inputs; j++)
            {
                v_read(n->inputs[j]->src, buf, sizeof(buf), nl, sub_wire_var);
                break;
            }
        }
        printf("    .%s(%s)%s\n", sp->name, buf, port_idx < nconn - 1 ? "," : "");
        port_idx++;
    }

    for (int si = 0; si < sub->num_nodes; si++)
    {
        node_t *sp = sub->nodes[si];
        if (sp->type != NODE_OUTPUT || !sp->is_public)
        {
            continue;
        }

        const char *port_name = sp->name;
        char nn[NAME_MAX] = "";
        const char *arg = "/*unused*/";
        int got = 0;
        for (int b = 0; b < n->num_output_bindings; b++)
        {
            if (strcmp(n->output_bindings[b].name, port_name) == 0)
            {
                node_t *ext = n->output_bindings[b].external_node;
                if (ext->type == NODE_OUTPUT && ext->is_public)
                {
                    snprintf(nn, sizeof(nn), "%s", ext->name);
                    sanitize(nn);
                    arg = nn;
                }
                else if (sub_wire_var[n->id])
                {
                    arg = sub_wire_var[n->id];
                }
                got = 1;
                break;
            }
        }
        if (!got && sub_wire_var[n->id])
        {
            arg = sub_wire_var[n->id];
        }
        printf("    .%s(%s)%s\n", sp->name, arg, port_idx < nconn - 1 ? "," : "");
        port_idx++;
    }
    printf("  );\n");
}

static void emit_v_expr(netlist_t *nl, node_t *n, int *emitted, char **sub_wire_var)
{
    if (emitted[n->id])
    {
        return;
    }
    emitted[n->id] = 1;

    switch (n->type)
    {
    case NODE_INPUT:
    case NODE_GND:
    case NODE_VCC:
    case NODE_CLOCK:
        return;
    case NODE_OUTPUT:
        if (n->num_inputs > 0)
        {
            emit_v_expr(nl, n->inputs[0]->src, emitted, sub_wire_var);
        }
        else
        {
            for (int i = 0; i < nl->num_nodes; i++)
            {
                node_t *s = nl->nodes[i];
                if (s->type == NODE_SUBCIRCUIT)
                {
                    for (int b = 0; b < s->num_output_bindings; b++)
                    {
                        if (s->output_bindings[b].external_node == n)
                        {
                            emit_v_expr(nl, s, emitted, sub_wire_var);
                        }
                    }
                }
            }
        }
        return;
    case NODE_SUBCIRCUIT:
        for (int b = 0; b < n->num_input_bindings; b++)
        {
            if (n->input_bindings[b].external_node)
            {
                emit_v_expr(nl, n->input_bindings[b].external_node, emitted, sub_wire_var);
            }
        }
        for (int j = 0; j < n->num_inputs; j++)
        {
            emit_v_expr(nl, n->inputs[j]->src, emitted, sub_wire_var);
        }
        emit_inst(nl, n, sub_wire_var);
        return;
    case NODE_DFF:
    case NODE_JKFF:
    case NODE_DLATCH:
        for (int i = 0; i < n->num_inputs; i++)
        {
            emit_v_expr(nl, n->inputs[i]->src, emitted, sub_wire_var);
        }
        emit_always(nl, n, sub_wire_var);
        return;
    default:
        break;
    }

    for (int i = 0; i < n->num_inputs; i++)
    {
        emit_v_expr(nl, n->inputs[i]->src, emitted, sub_wire_var);
    }

    char vn[NAME_MAX];
    node_base_var(n, vn, sizeof(vn));

    switch (n->type)
    {
    case NODE_NOT:
    case NODE_AND:
    case NODE_OR:
    case NODE_NAND:
    case NODE_NOR:
    case NODE_XOR:
    case NODE_XNOR:
    {
        if (n->num_inputs == 0)
        {
            break;
        }
        int is_neg = (n->type == NODE_NAND || n->type == NODE_NOR || n->type == NODE_XNOR);
        int is_not = (n->type == NODE_NOT);
        const char *op = (n->type == NODE_AND || n->type == NODE_NAND) ? "&"
                         : (n->type == NODE_OR || n->type == NODE_NOR) ? "|"
                                                                       : "^";
        printf("  assign %s = ", vn);
        if (is_neg || is_not)
        {
            printf("~(");
        }
        for (int i = 0; i < n->num_inputs; i++)
        {
            char e[NAME_MAX];
            v_read_bcast(n->inputs[i]->src, e, sizeof(e), nl, sub_wire_var, n->width);
            printf("%s%s", i ? op : "", e);
        }
        if (is_neg || is_not)
        {
            printf(")");
        }
        printf(";\n");
        break;
    }
    default:
        break;
    }
}

/* ---------------------------------------------------------------
   Module emitters
   --------------------------------------------------------------- */

static void emit_sub_module(netlist_t *nl, netlist_t *sub, const char *mname)
{
    (void)nl;
    printf("module %s (\n", mname);
    int first = 1;
    for (int si = 0; si < sub->num_nodes; si++)
    {
        node_t *n = sub->nodes[si];
        if (n->type != NODE_INPUT || !n->is_public)
        {
            continue;
        }

        printf("  %sinput ", first ? "" : ", ");
        print_v_range(n);
        printf("%s\n", n->name);
        first = 0;
    }
    for (int si = 0; si < sub->num_nodes; si++)
    {
        node_t *n = sub->nodes[si];
        if (n->type != NODE_OUTPUT || !n->is_public)
        {
            continue;
        }

        printf("  %soutput ", first ? "" : ", ");
        print_v_range(n);
        printf("%s\n", n->name);
        first = 0;
    }
    printf(");\n");

    /* Internal signals: gates -> wire, sequential -> reg, chained subs
       -> wire, internal labels driven by a sub -> wire. */
    char **sub_wire = calloc((size_t)sub->num_nodes, sizeof(char *));
    int wire_counter = 0;
    for (int i = 0; i < sub->num_nodes; i++)
    {
        node_t *n = sub->nodes[i];
        if (n->type == NODE_SUBCIRCUIT)
        {
            int needs = 0;
            for (int b = 0; b < n->num_output_bindings; b++)
            {
                node_t *ext = n->output_bindings[b].external_node;
                if (ext && (ext->type == NODE_SUBCIRCUIT || ext->type == NODE_OUTPUT))
                {
                    needs = 1;
                }
            }
            if (needs)
            {
                char wv[NAME_MAX];
                snprintf(wv, sizeof(wv), "_w%d", wire_counter++);
                sub_wire[n->id] = circ_strdup(wv);
                printf("  wire %s;\n", wv);
            }
        }
        else if (is_seq_node(n))
        {
            char vn[NAME_MAX];
            node_base_var(n, vn, sizeof(vn));
            printf("  reg %s = 1'b0;\n", vn);
        }
        else if (n->type != NODE_INPUT && n->type != NODE_OUTPUT && n->type != NODE_GND &&
                 n->type != NODE_VCC && n->type != NODE_CLOCK)
        {
            char vn[NAME_MAX];
            node_base_var(n, vn, sizeof(vn));
            printf("  wire ");
            print_v_range(n);
            printf("%s;\n", vn);
        }
    }

    size_t nsub = (size_t)sub->num_nodes;
    int *emitted = calloc(nsub, sizeof(int));
    for (int i = 0; i < sub->num_nodes; i++)
    {
        emit_v_expr(sub, sub->nodes[i], emitted, sub_wire);
    }
    /* public output assignments */
    for (int i = 0; i < sub->num_nodes; i++)
    {
        node_t *n = sub->nodes[i];
        if (n->type != NODE_OUTPUT || !n->is_public || n->num_inputs == 0)
        {
            continue;
        }

        char vn[NAME_MAX];
        v_read(n->inputs[0]->src, vn, sizeof(vn), sub, sub_wire);
        printf("  assign %s = %s;\n", n->name, vn);
    }
    free(emitted);
    for (int i = 0; i < sub->num_nodes; i++)
    {
        free(sub_wire[i]); // NOLINT: array sized sub->num_nodes
    }
    free(sub_wire);
    printf("endmodule\n\n");
}

void backend_v(netlist_t *nl)
{
    int num_in = 0, num_out = 0;
    node_t **inputs = netlist_get_inputs(nl, &num_in);
    node_t **outputs = netlist_get_outputs(nl, &num_out);

    sub_type_t *sub_types = NULL;
    int num_sub_types = 0, sub_types_cap = 0;
    collect_sub_types(nl, &sub_types, &num_sub_types, &sub_types_cap);
    for (int t = 0; t < num_sub_types; t++)
    {
        emit_sub_module(nl, sub_types[t].nl, sub_types[t].tname);
    }

    printf("module circuit (\n");
    for (int i = 0; i < num_in; i++)
    {
        printf("  input ");
        print_v_range(inputs[i]);
        printf("%s%s\n", inputs[i]->name, (i < num_in - 1 || num_out > 0) ? "," : "");
    }
    for (int i = 0; i < num_out; i++)
    {
        printf("  output ");
        print_v_range(outputs[i]);
        printf("%s%s\n", outputs[i]->name, i < num_out - 1 ? "," : "");
    }
    printf(");\n");

    char **sub_wire_var = calloc((size_t)nl->num_nodes, sizeof(char *));
    int wire_counter = 0;
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        if (n->type == NODE_SUBCIRCUIT)
        {
            int needs = 0;
            for (int b = 0; b < n->num_output_bindings; b++)
            {
                node_t *ext = n->output_bindings[b].external_node;
                if (ext && (ext->type == NODE_SUBCIRCUIT ||
                            (ext->type == NODE_OUTPUT && !ext->is_public) ||
                            (ext->type == NODE_OUTPUT && ext->is_public && ext->num_outputs > 0)))
                {
                    needs = 1;
                }
            }
            for (int j = 0; j < n->num_outputs; j++)
            {
                node_t *dst = n->outputs[j]->dst;
                if (dst->type == NODE_SUBCIRCUIT || dst->type == NODE_OUTPUT)
                {
                    needs = 1;
                }
            }
            if (needs)
            {
                char wv[NAME_MAX];
                snprintf(wv, sizeof(wv), "_w%d", wire_counter++);
                sub_wire_var[n->id] = circ_strdup(wv);
                printf("  wire %s;\n", wv);
            }
        }
        else if (is_seq_node(n))
        {
            char vn[NAME_MAX];
            node_base_var(n, vn, sizeof(vn));
            printf("  reg ");
            print_v_range(n);
            printf("%s = 1'b0;\n", vn);
        }
        else if (n->type != NODE_INPUT && n->type != NODE_OUTPUT && n->type != NODE_GND &&
                 n->type != NODE_VCC && n->type != NODE_CLOCK)
        {
            char vn[NAME_MAX];
            node_base_var(n, vn, sizeof(vn));
            printf("  wire ");
            print_v_range(n);
            printf("%s;\n", vn);
        }
    }

    int *emitted = calloc((size_t)nl->num_nodes, sizeof(int));
    for (int i = 0; i < nl->num_nodes; i++)
    {
        emit_v_expr(nl, nl->nodes[i], emitted, sub_wire_var);
    }
    /* public output assignments */
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        if (n->type != NODE_OUTPUT || !n->is_public || n->num_inputs == 0)
        {
            continue;
        }
        char vn[NAME_MAX];
        v_read(n->inputs[0]->src, vn, sizeof(vn), nl, sub_wire_var);
        printf("  assign %s = %s;\n", n->name, vn);
    }
    free(emitted);
    for (int i = 0; i < nl->num_nodes; i++)
    {
        free(sub_wire_var[i]);
    }
    free(sub_wire_var);
    printf("endmodule\n");
    free(inputs);
    free(outputs);
    free(sub_types);
}
