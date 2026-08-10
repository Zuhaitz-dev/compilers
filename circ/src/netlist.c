#include "netlist.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *node_type_names[] = {
    "INPUT", "OUTPUT", "AND",  "OR",     "NOT",   "NAND", "NOR", "XOR",
    "XNOR",  "DFF",    "JKFF", "DLATCH", "CLOCK", "GND",  "VCC", "SUBCIRCUIT",
};

int node_type_input_count(node_type_t t)
{
    switch (t)
    {
    case NODE_NOT:
        return 1;
    case NODE_AND:
    case NODE_OR:
    case NODE_NAND:
    case NODE_NOR:
    case NODE_XOR:
    case NODE_XNOR:
    case NODE_DFF:
    case NODE_DLATCH:
        return 2;
    case NODE_JKFF:
        return 3;
    default:
        return 0;
    }
}

int cell_connections(char c, int gate_cell)
{
    if (gate_cell)
    {
        return 0;
    }
    switch (c)
    {
    case '-':
        return CONN_L | CONN_R;
    case '|':
        return CONN_U | CONN_D;
    case '+':
        return CONN_L | CONN_R | CONN_U | CONN_D;
    case '>':
    case '<':
        return CONN_L | CONN_R;
    case '[':
        return CONN_L | CONN_U | CONN_D;
    case ']':
        return CONN_R | CONN_U | CONN_D;
    default:
        if (isalnum((unsigned char)c) || c == '_')
        {
            return CONN_L | CONN_R | CONN_U | CONN_D;
        }
        return 0;
    }
}

netlist_t *netlist_create(void)
{
    netlist_t *nl = calloc(1, sizeof(netlist_t));
    nl->node_cap = 64;
    nl->nodes = malloc(nl->node_cap * sizeof(node_t *));
    return nl;
}

node_t *netlist_add_node(netlist_t *nl, const char *name, node_type_t type, int x, int y)
{
    if (nl->num_nodes >= nl->node_cap)
    {
        nl->node_cap *= 2;
        nl->nodes = realloc(nl->nodes, nl->node_cap * sizeof(node_t *));
    }
    node_t *n = calloc(1, sizeof(node_t));
    strncpy(n->name, name, NAME_MAX - 1);
    n->name[NAME_MAX - 1] = '\0';
    n->type = type;
    n->width = 1;
    n->bus_msb = 0;
    n->bus_lsb = 0;
    n->x = x;
    n->id = nl->num_nodes;
    n->y = y;
    n->prev_clock = -1; /* unknown initial clock state */
    n->num_inputs_needed = node_type_input_count(type);
    nl->nodes[nl->num_nodes++] = n;
    return n;
}

int netlist_add_wire(netlist_t *nl, node_t *src, int src_pin, node_t *dst, int dst_pin)
{
    (void)nl;
    for (int i = 0; i < src->num_outputs; i++)
    {
        if (src->outputs[i]->dst == dst && src->outputs[i]->dst_pin == dst_pin)
        {
            return 0;
        }
    }

    wire_t *w = calloc(1, sizeof(wire_t));
    w->src = src;
    w->src_pin = src_pin;
    w->dst = dst;
    w->dst_pin = dst_pin;

    if (src->num_outputs >= src->output_cap)
    {
        src->output_cap = src->output_cap ? src->output_cap * 2 : 4;
        src->outputs = realloc(src->outputs, src->output_cap * sizeof(wire_t *));
    }
    src->outputs[src->num_outputs++] = w;

    if (dst->num_inputs >= dst->input_cap)
    {
        dst->input_cap = dst->input_cap ? dst->input_cap * 2 : 4;
        dst->inputs = realloc(dst->inputs, dst->input_cap * sizeof(wire_t *));
    }
    dst->inputs[dst->num_inputs++] = w;
    return 1;
}

netlist_t *netlist_clone(netlist_t *src)
{
    netlist_t *d = netlist_create();
    for (int i = 0; i < src->num_nodes; i++)
    {
        node_t *s = src->nodes[i];
        node_t *n = netlist_add_node(d, s->name, s->type, s->x, s->y);
        n->is_public = s->is_public;
        n->width = s->width;
        n->bus_msb = s->bus_msb;
        n->bus_lsb = s->bus_lsb;
        n->num_inputs_needed = s->num_inputs_needed;
        n->value = s->value;
        n->prev_clock = s->prev_clock;
        n->state = s->state;
    }
    for (int i = 0; i < src->num_nodes; i++)
    {
        node_t *s = src->nodes[i];
        node_t *dn = d->nodes[i];
        for (int j = 0; j < s->num_inputs; j++)
        {
            wire_t *w = s->inputs[j];
            netlist_add_wire(d, d->nodes[w->src->id], w->src_pin, dn, w->dst_pin);
        }
    }
    for (int i = 0; i < src->num_nodes; i++)
    {
        node_t *s = src->nodes[i];
        node_t *dn = d->nodes[i];
        if (s->sub_netlist)
        {
            dn->sub_netlist = netlist_clone(s->sub_netlist);
        }
        if (s->num_input_bindings > 0)
        {
            dn->input_bindings = malloc((size_t)s->num_input_bindings * sizeof(port_binding_t));
            for (int j = 0; j < s->num_input_bindings; j++)
            {
                dn->input_bindings[j] = s->input_bindings[j];
                if (s->input_bindings[j].external_node)
                {
                    dn->input_bindings[j].external_node =
                        d->nodes[s->input_bindings[j].external_node->id];
                }
            }
            dn->num_input_bindings = s->num_input_bindings;
            dn->input_bindings_cap = s->num_input_bindings;
        }
        if (s->num_output_bindings > 0)
        {
            dn->output_bindings = malloc((size_t)s->num_output_bindings * sizeof(port_binding_t));
            for (int j = 0; j < s->num_output_bindings; j++)
            {
                dn->output_bindings[j] = s->output_bindings[j];
                if (s->output_bindings[j].external_node)
                {
                    dn->output_bindings[j].external_node =
                        d->nodes[s->output_bindings[j].external_node->id];
                }
            }
            dn->num_output_bindings = s->num_output_bindings;
            dn->output_bindings_cap = s->num_output_bindings;
        }
        if (s->num_port_mappings > 0)
        {
            dn->port_mappings = malloc((size_t)s->num_port_mappings * sizeof(port_mapping_t));
            memcpy(dn->port_mappings, s->port_mappings,
                   (size_t)s->num_port_mappings * sizeof(port_mapping_t));
            dn->num_port_mappings = s->num_port_mappings;
            dn->port_mappings_cap = s->num_port_mappings;
        }
    }
    return d;
}

int netlist_get_inputs_count(netlist_t *nl)
{
    int count = 0;
    for (int i = 0; i < nl->num_nodes; i++)
    {
        if (nl->nodes[i]->type == NODE_INPUT)
        {
            count++;
        }
    }
    return count;
}

int netlist_get_outputs_count(netlist_t *nl)
{
    int count = 0;
    for (int i = 0; i < nl->num_nodes; i++)
    {
        if (nl->nodes[i]->type == NODE_OUTPUT)
        {
            count++;
        }
    }
    return count;
}

node_t **netlist_get_inputs(netlist_t *nl, int *count)
{
    *count = 0;
    for (int i = 0; i < nl->num_nodes; i++)
    {
        if (nl->nodes[i]->type == NODE_INPUT && nl->nodes[i]->is_public)
        {
            (*count)++;
        }
    }
    node_t **result = malloc(*count * sizeof(node_t *));
    int j = 0;
    for (int i = 0; i < nl->num_nodes; i++)
    {
        if (nl->nodes[i]->type == NODE_INPUT && nl->nodes[i]->is_public)
        {
            result[j++] = nl->nodes[i];
        }
    }
    return result;
}

node_t **netlist_get_outputs(netlist_t *nl, int *count)
{
    *count = 0;
    for (int i = 0; i < nl->num_nodes; i++)
    {
        if (nl->nodes[i]->type == NODE_OUTPUT && nl->nodes[i]->is_public)
        {
            (*count)++;
        }
    }
    node_t **result = malloc(*count * sizeof(node_t *));
    int j = 0;
    for (int i = 0; i < nl->num_nodes; i++)
    {
        if (nl->nodes[i]->type == NODE_OUTPUT && nl->nodes[i]->is_public)
        {
            result[j++] = nl->nodes[i];
        }
    }
    return result;
}

int netlist_has_sequential(netlist_t *nl)
{
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        if (n->type == NODE_DFF || n->type == NODE_JKFF || n->type == NODE_DLATCH)
        {
            return 1;
        }
        if (n->type == NODE_SUBCIRCUIT && n->sub_netlist && netlist_has_sequential(n->sub_netlist))
        {
            return 1;
        }
    }
    return 0;
}

node_t *netlist_find_node(netlist_t *nl, const char *name)
{
    for (int i = 0; i < nl->num_nodes; i++)
    {
        if (strcmp(nl->nodes[i]->name, name) == 0)
        {
            return nl->nodes[i];
        }
    }
    return NULL;
}

node_t *netlist_find_port(netlist_t *nl, const char *name)
{
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        if (n->is_public && (n->type == NODE_INPUT || n->type == NODE_OUTPUT) &&
            strcmp(n->name, name) == 0)
        {
            return n;
        }
    }
    return NULL;
}

void netlist_print(netlist_t *nl)
{
    printf("Netlist (%d nodes):\n", nl->num_nodes);
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        printf("  %s (%s) at (%d,%d) inputs=%d outputs=%d val=%d\n", n->name,
               node_type_names[n->type], n->x, n->y, n->num_inputs, n->num_outputs, n->value);
        for (int j = 0; j < n->num_inputs; j++)
        {
            printf("    input %d: <- %s\n", j, n->inputs[j]->src->name);
        }
        for (int j = 0; j < n->num_outputs; j++)
        {
            printf("    output %d: -> %s (pin %d)\n", j, n->outputs[j]->dst->name,
                   n->outputs[j]->dst_pin);
        }
        if (n->type == NODE_SUBCIRCUIT)
        {
            printf("    sub_netlist: %s (%d nodes)\n", n->sub_netlist ? "loaded" : "NULL",
                   n->sub_netlist ? n->sub_netlist->num_nodes : 0);
            for (int j = 0; j < n->num_input_bindings; j++)
            {
                printf("    input_binding[%d]: %s -> %s\n", j, n->input_bindings[j].name,
                       n->input_bindings[j].external_node ? n->input_bindings[j].external_node->name
                                                          : "NULL");
            }
            for (int j = 0; j < n->num_output_bindings; j++)
            {
                printf("    output_binding[%d]: %s -> %s\n", j, n->output_bindings[j].name,
                       n->output_bindings[j].external_node
                           ? n->output_bindings[j].external_node->name
                           : "NULL");
            }
        }
    }
}

void netlist_free(netlist_t *nl)
{
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        for (int j = 0; j < n->num_inputs; j++)
        {
            free(n->inputs[j]);
        }
        free(n->inputs);
        free(n->outputs);
        free(n->input_bindings);
        free(n->output_bindings);
        free(n->port_mappings);
        if (n->sub_netlist)
        {
            netlist_free(n->sub_netlist);
        }
        free(n);
    }
    free(nl->nodes);
    free(nl);
}
