#include "backend.h"
#include "backend_internal.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "circ_internal.h"

/* ---------------------------------------------------------------
   Identifier helpers
   --------------------------------------------------------------- */

static const char *st_prefix = "";

/* When set, emit plain assignments without change tracking and record
   only the prev-clock (used to initialize edge detection at zero state). */
static int init_pass = 0;

/* Value (read) expression for a signal: resolves public outputs and
   internal labels to their driver so ordering never matters. */
static void c_read(node_t *n, char *buf, int size, netlist_t *nl, char **sub_wire_var)
{
    if (n->type == NODE_INPUT)
    {
        if (init_pass)
        {
            snprintf(buf, size, "0"); /* reset state: all inputs low */
        }
        else
        {
            snprintf(buf, size, "%s", n->name);
        }
        sanitize(buf);
        return;
    }
    if (n->type == NODE_OUTPUT)
    {
        if (n->num_inputs > 0)
        {
            c_read(n->inputs[0]->src, buf, size, nl, sub_wire_var);
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
        node_base_var(n, buf, size);
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
    if (is_seq_node(n))
    {
        char base[NAME_MAX];
        snprintf(base, sizeof(base), "%s", buf);
        snprintf(buf, size, "%s%s_q", st_prefix, base);
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

static int sub_has_seq(netlist_t *nl)
{
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        if (is_seq_node(n))
        {
            return 1;
        }
        if (n->type == NODE_SUBCIRCUIT && n->sub_netlist && sub_has_seq(n->sub_netlist))
        {
            return 1;
        }
    }
    return 0;
}

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
                *types = realloc(*types, *cap * sizeof(sub_type_t));
            }
            (*types)[*num].nl = n->sub_netlist;
            strncpy((*types)[*num].tname, tname, NAME_MAX - 1);
            (*types)[*num].tname[NAME_MAX - 1] = '\0';
            (*num)++;
            collect_sub_types(n->sub_netlist, types, num, cap);
        }
    }
}

static void print_mask(int w)
{
    int m = (w >= 31) ? 0x7FFFFFFF : ((1 << w) - 1);
    printf("0x%X", (unsigned)m);
}

static void mask_literal(int w, char *buf, int size)
{
    int m = (w >= 31) ? 0x7FFFFFFF : ((1 << w) - 1);
    snprintf(buf, size, "0x%X", (unsigned)m);
}

/* Read expression for a signal, broadcasting a width-1 source that feeds
   a bus gate's data pin. */
static void c_read_bcast(node_t *src, char *buf, int size, netlist_t *nl, char **sub_wire_var,
                         int gate_width)
{
    char tmp[NAME_MAX];
    c_read(src, tmp, sizeof(tmp), nl, sub_wire_var);
    if (src->width == 1 && gate_width > 1)
    {
        char m[32];
        mask_literal(gate_width, m, sizeof(m));
        snprintf(buf, size, "(%s * %s)", tmp, m);
    }
    else
    {
        snprintf(buf, size, "%s", tmp);
    }
}

/* Emit one gate assignment, folding ALL input nets (junction fan-in).
   Negated gates (NOT/NAND/NOR/XNOR) are masked to width. */
static void emit_gate_expr(netlist_t *nl, node_t *n, char **sub_wire_var)
{
    char vname[NAME_MAX];
    node_base_var(n, vname, sizeof(vname));
    if (n->num_inputs == 0)
    {
        return;
    }
    int is_neg = (n->type == NODE_NAND || n->type == NODE_NOR || n->type == NODE_XNOR);
    int is_not = (n->type == NODE_NOT);
    const char *op = (n->type == NODE_AND || n->type == NODE_NAND) ? " & "
                     : (n->type == NODE_OR || n->type == NODE_NOR) ? " | "
                                                                   : " ^ ";

    if (init_pass)
    {
        printf("    %s = ", vname);
    }
    else
    {
        printf("    { int _t = ");
    }
    if (is_neg || is_not)
    {
        printf("~(");
    }
    for (int i = 0; i < n->num_inputs; i++)
    {
        char e[NAME_MAX];
        c_read_bcast(n->inputs[i]->src, e, sizeof(e), nl, sub_wire_var, n->width);
        printf("%s%s", i ? op : "", e);
    }
    if (is_neg || is_not)
    {
        printf(") & ");
        print_mask(n->width);
    }
    if (init_pass)
    {
        printf(";\n");
    }
    else
    {
        printf("; if (_t != %s) { %s = _t; _changed = 1; } }\n", vname, vname);
    }
}

/* ---------------------------------------------------------------
   Sequential state declarations
   --------------------------------------------------------------- */

static void emit_state_struct(netlist_t *sub, const char *tname)
{
    if (!sub_has_seq(sub))
    {
        return;
    }
    printf("typedef struct {\n");
    for (int i = 0; i < sub->num_nodes; i++)
    {
        node_t *n = sub->nodes[i];
        if (is_seq_node(n))
        {
            char vn[NAME_MAX];
            node_base_var(n, vn, sizeof(vn));
            printf("    int %s_q;\n", vn);
            if (n->type != NODE_DLATCH)
            {
                printf("    int %s_clk;\n", vn);
            }
        }
        else if (n->type == NODE_SUBCIRCUIT && n->sub_netlist && sub_has_seq(n->sub_netlist))
        {
            char cn[NAME_MAX];
            type_from_inst(n->name, cn, sizeof(cn));
            printf("    %s_st %s_st;\n", cn, n->name);
        }
    }
    printf("} %s_st;\n\n", tname);
}

/* ---------------------------------------------------------------
   Subcircuit call emitter
   --------------------------------------------------------------- */

static void emit_call(netlist_t *nl, node_t *n, char **sub_wire_var)
{
    netlist_t *sub = n->sub_netlist;
    char tname[NAME_MAX];
    type_from_inst(n->name, tname, sizeof(tname));

    int has_wire = sub_wire_var[n->id] != NULL;
    if (has_wire && !init_pass)
    {
        printf("    { int _t = %s;\n", sub_wire_var[n->id]);
    }
    printf("    %s(", tname);
    int first_arg = 1;
    if (sub_has_seq(sub))
    {
        printf("%s&%s_st", first_arg ? "" : ", ", n->name);
        first_arg = 0;
    }
    for (int si = 0; si < sub->num_nodes; si++)
    {
        node_t *sp = sub->nodes[si];
        if (sp->type != NODE_INPUT || !sp->is_public)
        {
            continue;
        }

        const char *port_name = sp->name;
        char buf[NAME_MAX] = "0";
        int got = 0;
        for (int b = 0; b < n->num_input_bindings; b++)
        {
            if (strcmp(n->input_bindings[b].name, port_name) == 0)
            {
                c_read(n->input_bindings[b].external_node, buf, sizeof(buf), nl, sub_wire_var);
                got = 1;
                break;
            }
        }
        if (!got)
        {
            for (int j = 0; j < n->num_inputs; j++)
            {
                c_read(n->inputs[j]->src, buf, sizeof(buf), nl, sub_wire_var);
                got = 1;
                break;
            }
        }
        printf("%s%s", first_arg ? "" : ", ", buf);
        first_arg = 0;
    }
    for (int si = 0; si < sub->num_nodes; si++)
    {
        node_t *sp = sub->nodes[si];
        if (sp->type != NODE_OUTPUT || !sp->is_public)
        {
            continue;
        }

        const char *port_name = sp->name;
        const char *arg = "0";
        int got = 0;
        for (int b = 0; b < n->num_output_bindings; b++)
        {
            if (strcmp(n->output_bindings[b].name, port_name) == 0)
            {
                node_t *ext = n->output_bindings[b].external_node;
                if (ext->type == NODE_OUTPUT && ext->is_public)
                {
                    char nn[NAME_MAX];
                    strncpy(nn, ext->name, NAME_MAX - 1);
                    nn[NAME_MAX - 1] = '\0';
                    sanitize(nn);
                    printf("%s%s", first_arg ? "" : ", ", nn);
                    first_arg = 0;
                    got = 1;
                    break;
                }
                else if (sub_wire_var[n->id])
                {
                    printf("%s&%s", first_arg ? "" : ", ", sub_wire_var[n->id]);
                    first_arg = 0;
                    got = 1;
                    break;
                }
            }
        }
        if (!got)
        {
            if (sub_wire_var[n->id])
            {
                printf("%s&%s", first_arg ? "" : ", ", sub_wire_var[n->id]);
            }
            else
            {
                printf("%s%s", first_arg ? "" : ", ", "0");
            }
            first_arg = 0;
        }
        (void)arg;
    }
    printf(");\n");
    if (has_wire && !init_pass)
    {
        printf("    if (_t != %s) _changed = 1; }\n", sub_wire_var[n->id]);
    }
}

/* ---------------------------------------------------------------
   Emitter: phase 1 = combinational + subcircuit calls + DLATCH,
   phase 2 = DFF/JKFF captures, phase 3 = public output assigns.
   --------------------------------------------------------------- */

static void emit_expr(netlist_t *nl, node_t *n, int *emitted, char **sub_wire_var);

static void seq_pin_exprs(netlist_t *nl, node_t *n, char *d, char *ck, char *j, char *k,
                          char **sub_wire_var)
{
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
                c_read_bcast(src, j, NAME_MAX, nl, sub_wire_var, n->width);
                break;
            case 1:
                c_read_bcast(src, k, NAME_MAX, nl, sub_wire_var, n->width);
                break;
            case 2:
                c_read(src, ck, NAME_MAX, nl, sub_wire_var);
                break;
            }
        }
        else if (pin == 0)
        {
            c_read_bcast(src, d, NAME_MAX, nl, sub_wire_var, n->width);
        }
        else
        {
            c_read(src, ck, NAME_MAX, nl, sub_wire_var);
        }
    }
}

static void emit_seq_capture(netlist_t *nl, node_t *n, char **sub_wire_var)
{
    char d[NAME_MAX], ck[NAME_MAX], j[NAME_MAX], k[NAME_MAX];
    seq_pin_exprs(nl, n, d, ck, j, k, sub_wire_var);
    char vn[NAME_MAX];
    node_base_var(n, vn, sizeof(vn));
    if (init_pass)
    {
        if (n->type != NODE_DLATCH)
        {
            printf("    %s%s_clk = %s;\n", st_prefix, vn, ck);
        }
        return;
    }
    if (n->type == NODE_DLATCH)
    {
        printf("    if (%s) { int _t = %s; if (_t != %s%s_q) { %s%s_q = _t; "
               "_changed = 1; } }\n",
               ck, d, st_prefix, vn, st_prefix, vn);
    }
    else if (n->type == NODE_DFF)
    {
        printf("    if (%s && !%s%s_clk) { int _t = %s; if (_t != %s%s_q) { "
               "%s%s_q = _t; _changed = 1; } }\n",
               ck, st_prefix, vn, d, st_prefix, vn, st_prefix, vn);
        printf("    %s%s_clk = %s;\n", st_prefix, vn, ck);
    }
    else
    {
        printf("    if (%s && !%s%s_clk) {\n", ck, st_prefix, vn);
        printf("        int _t = (%s & ~%s) | (%s & %s & ~%s%s_q) | "
               "(~%s & ~%s & %s%s_q);\n",
               j, k, j, k, st_prefix, vn, j, k, st_prefix, vn);
        printf("        _t &= ");
        print_mask(n->width);
        printf(";\n");
        printf("        if (_t != %s%s_q) { %s%s_q = _t; _changed = 1; }\n", st_prefix, vn,
               st_prefix, vn);
        printf("    }\n");
        printf("    %s%s_clk = %s;\n", st_prefix, vn, ck);
    }
}

/* Simultaneous edge capture in two phases: snapshot all next states using
   pre-edge values, then apply them all. Mirrors non-blocking Verilog. */
static void emit_seq_snapshot(netlist_t *nl, node_t *n, char **sub_wire_var)
{
    char d[NAME_MAX], ck[NAME_MAX], j[NAME_MAX], k[NAME_MAX];
    seq_pin_exprs(nl, n, d, ck, j, k, sub_wire_var);
    char vn[NAME_MAX];
    node_base_var(n, vn, sizeof(vn));
    if (n->type == NODE_DFF)
    {
        printf("    int %s_cap; if (%s && !%s%s_clk) %s_cap = %s;\n", vn, ck, st_prefix, vn, vn, d);
    }
    else if (n->type == NODE_JKFF)
    {
        printf("    int %s_cap; if (%s && !%s%s_clk) { %s_cap = "
               "(%s & ~%s) | (%s & %s & ~%s%s_q) | (~%s & ~%s & %s%s_q); "
               "%s_cap &= ",
               vn, ck, st_prefix, vn, vn, j, k, j, k, st_prefix, vn, j, k, st_prefix, vn, vn);
        print_mask(n->width);
        printf("; }\n");
    }
}

static void emit_seq_apply(netlist_t *nl, node_t *n, char **sub_wire_var)
{
    char d[NAME_MAX], ck[NAME_MAX], j[NAME_MAX], k[NAME_MAX];
    seq_pin_exprs(nl, n, d, ck, j, k, sub_wire_var);
    char vn[NAME_MAX];
    node_base_var(n, vn, sizeof(vn));
    printf("    if (%s && !%s%s_clk) { if (%s_cap != %s%s_q) { %s%s_q = "
           "%s_cap; _changed = 1; } }\n",
           ck, st_prefix, vn, vn, st_prefix, vn, st_prefix, vn, vn);
    printf("    %s%s_clk = %s;\n", st_prefix, vn, ck);
}

static void emit_expr(netlist_t *nl, node_t *n, int *emitted, char **sub_wire_var)
{
    if (emitted[n->id])
    {
        return;
    }
    emitted[n->id] = 1; /* mark before recursion to stop feedback re-entry */

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
            emit_expr(nl, n->inputs[0]->src, emitted, sub_wire_var);
        }
        else
        {
            /* driven by a subcircuit output binding */
            for (int i = 0; i < nl->num_nodes; i++)
            {
                node_t *s = nl->nodes[i];
                if (s->type == NODE_SUBCIRCUIT)
                {
                    for (int b = 0; b < s->num_output_bindings; b++)
                    {
                        if (s->output_bindings[b].external_node == n)
                        {
                            emit_expr(nl, s, emitted, sub_wire_var);
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
                emit_expr(nl, n->input_bindings[b].external_node, emitted, sub_wire_var);
            }
        }
        for (int j = 0; j < n->num_inputs; j++)
        {
            emit_expr(nl, n->inputs[j]->src, emitted, sub_wire_var);
        }
        emit_call(nl, n, sub_wire_var);
        return;
    case NODE_DFF:
    case NODE_JKFF:
        /* capture is deferred to phase 2; emit combinational inputs only */
        for (int i = 0; i < n->num_inputs; i++)
        {
            emit_expr(nl, n->inputs[i]->src, emitted, sub_wire_var);
        }
        return;
    default:
        break;
    }

    for (int i = 0; i < n->num_inputs; i++)
    {
        emit_expr(nl, n->inputs[i]->src, emitted, sub_wire_var);
    }

    char vname[NAME_MAX];
    node_base_var(n, vname, sizeof(vname));

    switch (n->type)
    {
    case NODE_NOT:
    case NODE_AND:
    case NODE_OR:
    case NODE_NAND:
    case NODE_NOR:
    case NODE_XOR:
    case NODE_XNOR:
        emit_gate_expr(nl, n, sub_wire_var);
        break;
    case NODE_DLATCH:
        /* level-sensitive: capture inline so consumers see fresh value */
        emit_seq_capture(nl, n, sub_wire_var);
        break;
    default:
        break;
    }
    emitted[n->id] = 1;
}

/* Emit phase 2 (DFF/JKFF captures) and phase 3 (public output assigns). */
static void emit_tail(netlist_t *nl, char **sub_wire_var)
{
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        if (n->type == NODE_DFF || n->type == NODE_JKFF)
        {
            emit_seq_snapshot(nl, n, sub_wire_var);
        }
    }
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        if (n->type == NODE_DFF || n->type == NODE_JKFF)
        {
            emit_seq_apply(nl, n, sub_wire_var);
        }
    }
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        if (n->type != NODE_OUTPUT || !n->is_public || n->num_inputs == 0)
        {
            continue;
        }
        char pname[NAME_MAX], vname[NAME_MAX];
        strncpy(pname, n->name, NAME_MAX - 1);
        pname[NAME_MAX - 1] = '\0';
        sanitize(pname);
        c_read(n->inputs[0]->src, vname, sizeof(vname), nl, sub_wire_var);
        /* Guard: an unbound subcircuit output is passed as NULL. */
        printf("    if (%s) *%s = %s;\n", pname, pname, vname);
    }
}

/* ---------------------------------------------------------------
   Subcircuit function emitter
   --------------------------------------------------------------- */

static void emit_sub_func(netlist_t *nl, netlist_t *sub, const char *tname)
{
    (void)nl;
    int has_seq = sub_has_seq(sub);
    printf("static void %s(", tname);
    int first = 1;
    if (has_seq)
    {
        printf("%s_st *st", tname);
        first = 0;
    }
    for (int si = 0; si < sub->num_nodes; si++)
    {
        node_t *n = sub->nodes[si];
        if (n->type != NODE_INPUT || !n->is_public)
        {
            continue;
        }
        char nn[NAME_MAX];
        strncpy(nn, n->name, NAME_MAX - 1);
        nn[NAME_MAX - 1] = '\0';
        sanitize(nn);
        printf("%sint %s", first ? "" : ", ", nn);
        first = 0;
    }
    for (int si = 0; si < sub->num_nodes; si++)
    {
        node_t *n = sub->nodes[si];
        if (n->type != NODE_OUTPUT || !n->is_public)
        {
            continue;
        }
        char nn[NAME_MAX];
        strncpy(nn, n->name, NAME_MAX - 1);
        nn[NAME_MAX - 1] = '\0';
        sanitize(nn);
        printf("%sint *%s", first ? "" : ", ", nn);
        first = 0;
    }
    printf(") {\n");

    const char *saved_prefix = st_prefix;
    if (has_seq)
    {
        st_prefix = "st->";
    }

    int *emitted = calloc(sub->num_nodes, sizeof(int));
    char **sub_wire = calloc((size_t)sub->num_nodes, sizeof(char *));
    int wire_counter = 0;

    for (int i = 0; i < sub->num_nodes; i++)
    {
        node_t *n = sub->nodes[i];
        if (n->type != NODE_SUBCIRCUIT)
        {
            continue;
        }
        int needs_wire = 0;
        for (int b = 0; b < n->num_output_bindings; b++)
        {
            node_t *ext = n->output_bindings[b].external_node;
            if (ext && (ext->type == NODE_SUBCIRCUIT || ext->type == NODE_OUTPUT))
            {
                needs_wire = 1;
            }
        }
        if (needs_wire)
        {
            char wv[NAME_MAX];
            snprintf(wv, sizeof(wv), "_w%d", wire_counter++);
            sub_wire[n->id] = circ_strdup(wv);
            printf("    int %s;\n", wv);
        }
    }

    /* Declare gate/sequential variables up front. */
    for (int i = 0; i < sub->num_nodes; i++)
    {
        node_t *n = sub->nodes[i];
        if (n->type == NODE_INPUT || n->type == NODE_OUTPUT || n->type == NODE_SUBCIRCUIT ||
            n->type == NODE_GND || n->type == NODE_VCC || n->type == NODE_CLOCK)
        {
            continue;
        }
        char vn[NAME_MAX];
        node_base_var(n, vn, sizeof(vn));
        printf("    int %s;\n", vn);
    }

    printf("    int _changed = 1, _guard = 0;\n");
    printf("    static int _inited = 0;\n");
    printf("    if (!_inited) {\n");

    /* Init pass: compute combinational values at reset state and record
       the initial clock edges, matching the simulator's init. */
    init_pass = 1;
    {
        int *e0 = calloc(sub->num_nodes, sizeof(int));
        for (int i = 0; i < sub->num_nodes; i++)
        {
            emit_expr(sub, sub->nodes[i], e0, sub_wire);
        }
        for (int i = 0; i < sub->num_nodes; i++)
        {
            if (is_seq_node(sub->nodes[i]))
            {
                emit_seq_capture(sub, sub->nodes[i], sub_wire);
            }
        }
        free(e0);
    }
    init_pass = 0;
    printf("        _inited = 1;\n    }\n");

    printf("    while (_changed && _guard++ < %d) {\n", sub->num_nodes + 4);
    printf("        _changed = 0;\n");

    for (int i = 0; i < sub->num_nodes; i++)
    {
        emit_expr(sub, sub->nodes[i], emitted, sub_wire);
    }
    emit_tail(sub, sub_wire);

    printf("    }\n");
    for (int i = 0; i < sub->num_nodes; i++)
    {
        free(sub_wire[i]);
    }
    free(sub_wire);
    free(emitted);
    st_prefix = saved_prefix;
    printf("}\n\n");
}

/* ---------------------------------------------------------------
   Top-level circuit
   --------------------------------------------------------------- */

void backend_c(netlist_t *nl)
{
    printf("#include <stdint.h>\n\n");

    sub_type_t *sub_types = NULL;
    int num_sub_types = 0, sub_types_cap = 0;
    collect_sub_types(nl, &sub_types, &num_sub_types, &sub_types_cap);

    for (int t = 0; t < num_sub_types; t++)
    {
        emit_state_struct(sub_types[t].nl, sub_types[t].tname);
    }
    for (int t = 0; t < num_sub_types; t++)
    {
        emit_sub_func(nl, sub_types[t].nl, sub_types[t].tname);
    }

    int num_in = 0, num_out = 0;
    node_t **inputs = netlist_get_inputs(nl, &num_in);
    node_t **outputs = netlist_get_outputs(nl, &num_out);

    printf("void circuit(");
    int first = 1;
    for (int i = 0; i < num_in; i++)
    {
        char nn[NAME_MAX];
        strncpy(nn, inputs[i]->name, NAME_MAX - 1);
        nn[NAME_MAX - 1] = '\0';
        sanitize(nn);
        printf("%sint %s", first ? "" : ", ", nn);
        first = 0;
    }
    for (int i = 0; i < num_out; i++)
    {
        char nn[NAME_MAX];
        strncpy(nn, outputs[i]->name, NAME_MAX - 1);
        nn[NAME_MAX - 1] = '\0';
        sanitize(nn);
        printf("%sint *%s", first ? "" : ", ", nn);
        first = 0;
    }
    printf(") {\n");

    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        if (is_seq_node(n))
        {
            char vn[NAME_MAX];
            node_base_var(n, vn, sizeof(vn));
            printf("    static int %s_q = 0;\n", vn);
            if (n->type != NODE_DLATCH)
            {
                printf("    static int %s_clk = 0;\n", vn);
            }
        }
        else if (n->type == NODE_SUBCIRCUIT && n->sub_netlist && sub_has_seq(n->sub_netlist))
        {
            char tname[NAME_MAX];
            type_from_inst(n->name, tname, sizeof(tname));
            printf("    static %s_st %s_st;\n", tname, n->name);
        }
    }

    char **sub_wire_var = calloc((size_t)nl->num_nodes, sizeof(char *));
    int wire_counter = 0;

    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        if (n->type != NODE_SUBCIRCUIT)
        {
            continue;
        }
        int needs_wire = 0;
        for (int b = 0; b < n->num_output_bindings; b++)
        {
            node_t *ext = n->output_bindings[b].external_node;
            if (ext &&
                (ext->type == NODE_SUBCIRCUIT || (ext->type == NODE_OUTPUT && !ext->is_public) ||
                 (ext->type == NODE_OUTPUT && ext->is_public && ext->num_outputs > 0)))
            {
                needs_wire = 1;
            }
        }
        for (int j = 0; j < n->num_outputs; j++)
        {
            node_t *dst = n->outputs[j]->dst;
            if (dst->type == NODE_SUBCIRCUIT || dst->type == NODE_OUTPUT)
            {
                needs_wire = 1;
            }
        }
        if (needs_wire)
        {
            char wv[NAME_MAX];
            snprintf(wv, sizeof(wv), "_w%d", wire_counter++);
            sub_wire_var[n->id] = circ_strdup(wv);
            printf("    int %s;\n", wv);
        }
    }

    int *emitted = calloc(nl->num_nodes, sizeof(int));

    /* Declare gate/sequential variables up front. */
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        if (n->type == NODE_INPUT || n->type == NODE_OUTPUT || n->type == NODE_SUBCIRCUIT ||
            n->type == NODE_GND || n->type == NODE_VCC || n->type == NODE_CLOCK)
        {
            continue;
        }
        char vn[NAME_MAX];
        node_base_var(n, vn, sizeof(vn));
        printf("    int %s;\n", vn);
    }

    printf("    int _changed = 1, _guard = 0;\n");
    printf("    static int _inited = 0;\n");
    printf("    if (!_inited) {\n");

    /* Init pass: compute combinational values at reset state and record
       the initial clock edges, matching the simulator's init. */
    init_pass = 1;
    {
        int *e0 = calloc(nl->num_nodes, sizeof(int));
        for (int i = 0; i < nl->num_nodes; i++)
        {
            emit_expr(nl, nl->nodes[i], e0, sub_wire_var);
        }
        for (int i = 0; i < nl->num_nodes; i++)
        {
            if (is_seq_node(nl->nodes[i]))
            {
                emit_seq_capture(nl, nl->nodes[i], sub_wire_var);
            }
        }
        free(e0);
    }
    init_pass = 0;
    printf("        _inited = 1;\n    }\n");

    printf("    while (_changed && _guard++ < %d) {\n", nl->num_nodes + 4);
    printf("        _changed = 0;\n");

    for (int i = 0; i < nl->num_nodes; i++)
    {
        emit_expr(nl, nl->nodes[i], emitted, sub_wire_var);
    }
    emit_tail(nl, sub_wire_var);

    printf("    }\n");
    for (int i = 0; i < nl->num_nodes; i++)
    {
        free(sub_wire_var[i]);
    }
    free(sub_wire_var);
    free(emitted);
    printf("}\n");
    free(inputs);
    free(outputs);
    free(sub_types);
}

/* Emit a standalone program: the circuit() library plus a main() that
   mirrors --sim (NAME=VALUE inputs, --cycles=N, auto-toggled CLK). */
void backend_c_driver(netlist_t *nl)
{
    int num_in = 0, num_out = 0;
    node_t **ins = netlist_get_inputs(nl, &num_in);
    node_t **outs = netlist_get_outputs(nl, &num_out);

    /* The clock input is the one named CLK (case-insensitive, like --sim). */
    int clk_idx = -1;
    for (int i = 0; i < num_in; i++)
    {
        if (strcasecmp(ins[i]->name, "CLK") == 0)
        {
            clk_idx = i;
        }
    }

    printf("#include <stdio.h>\n");
    printf("#include <string.h>\n");
    backend_c(nl); /* prints <stdint.h> + the circuit() library */

    printf("int main(int argc, char **argv) {\n");
    for (int i = 0; i < num_in; i++)
    {
        char nn[NAME_MAX];
        strncpy(nn, ins[i]->name, NAME_MAX - 1);
        nn[NAME_MAX - 1] = '\0';
        sanitize(nn);
        printf("    int %s = 0;\n", nn);
    }
    for (int i = 0; i < num_out; i++)
    {
        char nn[NAME_MAX];
        strncpy(nn, outs[i]->name, NAME_MAX - 1);
        nn[NAME_MAX - 1] = '\0';
        sanitize(nn);
        printf("    int %s_out = 0;\n", nn);
    }
    printf("    int cycles = 1;\n");
    printf("    int clk_started = 0;\n");
    printf("    for (int i = 1; i < argc; i++) {\n");
    printf("        if (sscanf(argv[i], \"--cycles=%%d\", &cycles) == 1) "
           "continue;\n");
    printf("        char nm[64]; int val;\n");
    printf("        if (sscanf(argv[i], \"%%63[^=]=%%d\", nm, &val) == 2) {\n");
    for (int i = 0; i < num_in; i++)
    {
        char nn[NAME_MAX], mask[32];
        strncpy(nn, ins[i]->name, NAME_MAX - 1);
        nn[NAME_MAX - 1] = '\0';
        sanitize(nn);
        mask_literal(ins[i]->width, mask, sizeof(mask));
        printf("            if (strcmp(nm, \"%s\") == 0) { %s = val & %s;", ins[i]->name, nn, mask);
        if (i == clk_idx)
        {
            printf(" clk_started = 1;");
        }
        printf(" }\n");
        if (i + 1 < num_in)
        {
            printf("            else ");
        }
    }
    printf("        }\n");
    printf("    }\n");
    printf("    if (!clk_started) {\n");
    if (clk_idx >= 0)
    {
        char nn[NAME_MAX];
        strncpy(nn, ins[clk_idx]->name, NAME_MAX - 1);
        nn[NAME_MAX - 1] = '\0';
        sanitize(nn);
        printf("        %s = 0;\n", nn);
    }
    printf("    }\n");

    /* build the call arg list */
    {
        char args[1024] = "";
        int first_arg = 1;
        for (int i = 0; i < num_in; i++)
        {
            char nn[NAME_MAX];
            strncpy(nn, ins[i]->name, NAME_MAX - 1);
            nn[NAME_MAX - 1] = '\0';
            sanitize(nn);
            if (!first_arg)
            {
                strncat(args, ", ", sizeof(args) - strlen(args) - 1);
            }
            strncat(args, nn, sizeof(args) - strlen(args) - 1);
            first_arg = 0;
        }
        for (int i = 0; i < num_out; i++)
        {
            char nn[NAME_MAX], tmp[NAME_MAX + 8];
            strncpy(nn, outs[i]->name, NAME_MAX - 1);
            nn[NAME_MAX - 1] = '\0';
            sanitize(nn);
            if (!first_arg)
            {
                strncat(args, ", ", sizeof(args) - strlen(args) - 1);
            }
            snprintf(tmp, sizeof(tmp), "&%s_out", nn);
            strncat(args, tmp, sizeof(args) - strlen(args) - 1);
            first_arg = 0;
        }
        printf("    circuit(%s);\n", args);
        printf("    for (int c = 0; c < cycles; c++) {\n");
        if (clk_idx >= 0)
        {
            char nn[NAME_MAX];
            strncpy(nn, ins[clk_idx]->name, NAME_MAX - 1);
            nn[NAME_MAX - 1] = '\0';
            sanitize(nn);
            printf("        if (c > 0 || !clk_started) %s = !%s;\n", nn, nn);
        }
        printf("        circuit(%s);\n", args);
        printf("    }\n");
    }

    if (num_out == 0)
    {
        printf("    printf(\"\\n\");\n");
    }
    else
    {
        printf("    printf(\"");
        for (int i = 0; i < num_out; i++)
        {
            printf("%s=%%d ", outs[i]->name);
        }
        printf("\\n\", ");
        for (int i = 0; i < num_out; i++)
        {
            char nn[NAME_MAX];
            strncpy(nn, outs[i]->name, NAME_MAX - 1);
            nn[NAME_MAX - 1] = '\0';
            sanitize(nn);
            printf("%s%s_out", i ? ", " : "", nn);
        }
        printf(");\n");
    }
    printf("    return 0;\n");
    printf("}\n");

    free(ins);
    free(outs);
}
