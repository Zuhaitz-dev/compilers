#include "vlayout.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NODES 512

/* ---------------------------------------------------------------
   Simple, robust layout:
   - each gate gets its own row block, columns to the right
   - each gate input pin is wired locally to a label on the pin's row
   - each gate output is wired locally to a label on the gate's row
   Signals connect purely by name (the parser merges repeated labels),
   so no cross-gate routing is needed and wires never cross.
   --------------------------------------------------------------- */

static void label_with_marker(node_t *n, char *buf, int size)
{
    strncpy(buf, n->name, size - 2);
    buf[size - 1] = '\0';
    int len = (int)strlen(buf);
    if (n->type == NODE_INPUT && n->is_public && len < size - 1)
    {
        buf[len++] = '>';
    }
    else if (n->type == NODE_OUTPUT && n->is_public && len < size - 1)
    {
        buf[len++] = '<';
    }
    buf[len] = '\0';
}

static void vset(grid_t *g, char c, int x, int y)
{
    if (x >= 0 && x < g->width && y >= 0 && y < g->height)
    {
        g->data[y][x] = c;
    }
}

/* Draw the label text and return the column just past it. */
static int vdraw_label(grid_t *g, node_t *sig, int col, int row)
{
    char buf[NAME_MAX];
    label_with_marker(sig, buf, sizeof(buf));
    int len = (int)strlen(buf);
    for (int i = 0; i < len; i++)
    {
        vset(g, buf[i], col + i, row);
    }
    return col + len;
}

static int node_is_gate(node_t *n)
{
    return n->type != NODE_INPUT && n->type != NODE_OUTPUT && n->type != NODE_GND &&
           n->type != NODE_VCC && n->type != NODE_CLOCK;
}

grid_t *vlayout(netlist_t *nl, const char *top_name)
{
    (void)top_name;

    /* Collect gates in netlist order (already roughly topological for
       the nets vbuild produces; sequential gates act as barriers). */
    node_t *gates[MAX_NODES];
    int ngates = 0;
    for (int i = 0; i < nl->num_nodes && ngates < MAX_NODES; i++)
    {
        if (node_is_gate(nl->nodes[i]))
        {
            gates[ngates++] = nl->nodes[i];
        }
    }

    const int GATE_COL = 44;
    const int PIN_GAP = 8;
    const int ROW_STEP = 6;

    int H = ngates * ROW_STEP + 4;
    int W = GATE_COL + 40;
    grid_t *g = calloc(1, sizeof(grid_t));
    g->width = W;
    g->height = H;
    g->data = malloc((size_t)H * sizeof(char *));
    for (int y = 0; y < H; y++)
    {
        g->data[y] = malloc((size_t)W + 1);
        memset(g->data[y], ' ', (size_t)W);
        g->data[y][W] = '\0';
    }

    for (int gi = 0; gi < ngates; gi++)
    {
        node_t *n = gates[gi];
        int R = 1 + gi * ROW_STEP;
        const char *gn = node_type_names[n->type];
        int gnl = (int)strlen(gn);
        int gx = GATE_COL;
        int gw = gnl + 2;

        vset(g, '[', gx, R);
        for (int c = 0; c < gnl; c++)
        {
            vset(g, gn[c], gx + 1 + c, R);
        }
        vset(g, ']', gx + gnl + 1, R);

        /* Input pins: label on the pin's row, wired in from the left. */
        for (int p = 0; p < n->num_inputs; p++)
        {
            node_t *src = n->inputs[p]->src;
            if (!src)
            {
                continue;
            }
            int pin_row = R + p;
            int sock_col = (p == 0) ? (gx - 1) : (gx + p - 1);
            int lc = gx - 4 - p * PIN_GAP;
            int lend = vdraw_label(g, src, lc, pin_row);
            /* wire from label to socket (clamp to adjacent) */
            int start = lend;
            int end = sock_col - 1;
            if (start <= end)
            {
                for (int cx = start; cx <= end; cx++)
                {
                    if (g->data[pin_row][cx] == ' ')
                    {
                        vset(g, '-', cx, pin_row);
                    }
                }
            }
            /* socket marker */
            if (sock_col >= 0 && sock_col < W && g->data[pin_row][sock_col] == ' ')
            {
                vset(g, '-', sock_col, pin_row);
            }
        }

        /* Output: label to the right of the gate. */
        if (n->num_outputs > 0)
        {
            node_t *dst = n->outputs[0]->dst;
            if (dst)
            {
                int ocol = gx + gw + 1;
                int lend = vdraw_label(g, dst, ocol, R);
                (void)lend;
                for (int cx = gx + gw; cx < ocol; cx++)
                {
                    if (g->data[R][cx] == ' ')
                    {
                        vset(g, '-', cx, R);
                    }
                }
            }
        }
    }

    return g;
}

void vlayout_emit(grid_t *g)
{
    for (int y = 0; y < g->height; y++)
    {
        char *p = g->data[y] + g->width - 1;
        while (p > g->data[y] && *p == ' ')
        {
            p--;
        }
        *(p + 1) = '\0';
        puts(g->data[y]);
    }
}
