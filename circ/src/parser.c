#include "parser.h"
#include "arena.h"
#include "circ_internal.h"
#include "subcircuit.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ================================================================
   Pipeline context — shared state across all parse passes
   ================================================================ */

typedef struct
{
    grid_t *g;
    netlist_t *nl;
    int **gate_map;
    int **bus_cell; /* bus label cells -> node index (-1 = none) */
    int gate_idx;
    int W, H;
    char search_dir[512];
    circ_error_t *err;
    arena_t arena;
    unsigned char *covered; /* cells reached by any wire BFS */
} parse_ctx_t;

/* ================================================================
   Gate definitions
   ================================================================ */

typedef struct
{
    const char *name;
    node_type_t type;
    int num_inputs;
} gate_def_t;

static const gate_def_t GATE_DEFS[] = {
    {"AND", NODE_AND, 2},   {"OR", NODE_OR, 2},
    {"NOT", NODE_NOT, 1},   {"NAND", NODE_NAND, 2},
    {"NOR", NODE_NOR, 2},   {"XOR", NODE_XOR, 2},
    {"XNOR", NODE_XNOR, 2}, {"DFF", NODE_DFF, 2},
    {"JKFF", NODE_JKFF, 3}, {"DLATCH", NODE_DLATCH, 2},
    {"GND", NODE_GND, 0},   {"VCC", NODE_VCC, 0},
    {NULL, 0, 0},
};

static const gate_def_t *find_gate_def(const char *name)
{
    for (int i = 0; GATE_DEFS[i].name; i++)
    {
        if (strcasecmp(name, GATE_DEFS[i].name) == 0)
        {
            return &GATE_DEFS[i];
        }
    }
    return NULL;
}

/* ================================================================
   Port name helpers
   ================================================================ */

static int port_start(unsigned char c)
{
    return isupper(c) || c == '_';
}
static int port_cont(unsigned char c)
{
    return isalnum(c) || c == '_';
}

/* Extract a port name spanning (cx,cy) into buf */
static void extract_port_name(const grid_t *g, int cx, int cy, char *buf, int size)
{
    int s = cx;
    while (s > 0 && port_cont(grid_get(g, s - 1, cy)))
    {
        s--;
    }
    int e = cx;
    while (e < g->width - 1 && port_cont(grid_get(g, e + 1, cy)))
    {
        e++;
    }
    int len = e - s + 1;
    if (len >= size)
    {
        len = size - 1;
    }
    strncpy(buf, &g->data[cy][s], len);
    buf[len] = '\0';
}

static int has_adjacent_wire(const grid_t *g, int x, int y, int extent)
{
    static const int DX[] = {-1, 1, 0, 0};
    static const int DY[] = {0, 0, -1, 1};
    for (int d = 0; d < 4; d++)
    {
        for (int ox = 0; ox < extent; ox++)
        {
            char c = grid_get(g, x + ox + DX[d], y + DY[d]);
            if (c == '-' || c == '|' || c == '+' || c == '[' || c == ']' || c == '>' || c == '<')
            {
                return 1;
            }
        }
    }
    return 0;
}

/* ================================================================
   Mapping string parser
   ================================================================ */

static port_mapping_t *parse_mappings(const char *label, int *count)
{
    *count = 0;
    const char *p = strchr(label, '(');
    if (!p)
    {
        return NULL;
    }
    const char *c = strchr(p + 1, ')');
    if (!c)
    {
        return NULL;
    }
    int mlen = (int)(c - p - 1);
    if (mlen > 511)
    {
        mlen = 511;
    }

    /* Count name=value pairs first, then allocate. */
    int n = 0;
    {
        char buf[512];
        strncpy(buf, p + 1, mlen);
        buf[mlen] = '\0';
        char *tok = strtok(buf, " ,");
        while (tok)
        {
            if (strchr(tok, '='))
            {
                n++;
            }
            tok = strtok(NULL, " ,");
        }
    }
    if (n == 0)
    {
        return NULL;
    }

    port_mapping_t *maps = calloc((size_t)n, sizeof(port_mapping_t));
    char buf[512];
    strncpy(buf, p + 1, mlen);
    buf[mlen] = '\0';
    int idx = 0;
    char *tok = strtok(buf, " ,");
    while (tok && idx < n)
    {
        char *eq = strchr(tok, '=');
        if (eq)
        {
            *eq = '\0';
            strncpy(maps[idx].sub_port_name, tok, 63);
            strncpy(maps[idx].external_name, eq + 1, 63);
            idx++;
        }
        tok = strtok(NULL, " ,");
    }
    *count = n;
    return maps;
}

/* ================================================================
   PASS 0: Reject unsupported characters with clear messages
   ================================================================ */

static void pass_reject_diagonals(parse_ctx_t *ctx)
{
    for (int y = 0; y < ctx->H; y++)
    {
        for (int x = 0; x < ctx->W; x++)
        {
            char c = grid_get(ctx->g, x, y);
            if (c == '/' || c == '\\')
            {
                if (ctx->err)
                {
                    error_set_msg(ctx->err,
                                  "diagonal wire '%c' at (%d,%d) is not "
                                  "supported; route with '+' and '|' instead",
                                  c, x, y);
                }
                return;
            }
        }
    }
}

/* ================================================================
   PASS 1: Find gates and subcircuits
   ================================================================ */

static void pass_find_gates(parse_ctx_t *ctx)
{
    int W = ctx->W, H = ctx->H;
    grid_t *g = ctx->g;
    netlist_t *nl = ctx->nl;
    int **gm = ctx->gate_map;

    for (int y = 0; y < H; y++)
    {
        for (int x = 0; x < W; x++)
        {
            if (grid_get(g, x, y) != '[')
            {
                continue;
            }

            int end = x + 1;
            while (end < W && grid_get(g, end, y) != ']')
            {
                end++;
            }
            if (end >= W)
            {
                continue;
            }

            int len = end - x - 1;
            if (len <= 0)
            {
                continue;
            }

            /* A bus range like [3:0] is a label suffix, not a gate. */
            {
                int is_bus = 0;
                for (int i = 1; i < len; i++)
                {
                    if (g->data[y][x + i] == ':')
                    {
                        int ok = 1;
                        for (int j = 1; j < len; j++)
                        {
                            if (j != i && !isdigit((unsigned char)g->data[y][x + j]))
                            {
                                ok = 0;
                            }
                        }
                        is_bus = ok;
                        break;
                    }
                }
                if (is_bus)
                {
                    continue;
                }
            }

            /* The label may be long (subcircuit port mappings); do not
               truncate it, or mappings are silently lost. */
            char *raw = malloc((size_t)len + 1);
            memcpy(raw, &g->data[y][x + 1], (size_t)len);
            raw[len] = '\0';

            char *name = circ_strdup(raw);
            char *paren = strchr(name, '(');
            if (paren)
            {
                *paren = '\0';
            }
            if (strlen(name) >= NAME_MAX)
            {
                if (ctx->err)
                {
                    error_set_msg(ctx->err,
                                  "gate/subcircuit name '%s...' at (%d,%d) "
                                  "is too long (max %d)",
                                  name, x, y, NAME_MAX - 1);
                }
                free(raw);
                free(name);
                goto done;
            }

            /* Parse optional port mapping (name(key=val...)) */
            int num_mappings = 0;
            port_mapping_t *mappings = NULL;
            if (paren)
            {
                mappings = parse_mappings(raw, &num_mappings);
            }

            /* Built-in gate? */
            const gate_def_t *def = find_gate_def(name);
            if (def)
            {
                node_t *node = netlist_add_node(nl, name, def->type, x, y);
                node->width = 0; /* inferred from connected nets */
                node->num_inputs_needed = def->num_inputs;
                for (int cx = x; cx <= end; cx++)
                {
                    gm[y][cx] = ctx->gate_idx;
                }
                ctx->gate_idx++;
                free(mappings);
                free(raw);
                free(name);
                continue;
            }

            /* Subcircuit? */
            netlist_t *sub_nl = subcircuit_load(name, ctx->search_dir);
            if (sub_nl)
            {
                /* Find next available instance name */
                char iname[256];
                for (int c = 0;; c++)
                {
                    snprintf(iname, sizeof(iname), "%s_%d", name, c);
                    if (!netlist_find_node(nl, iname))
                    {
                        break;
                    }
                }
                node_t *node = netlist_add_node(nl, iname, NODE_SUBCIRCUIT, x, y);
                node->sub_netlist = sub_nl;
                if (num_mappings > 0)
                {
                    node->port_mappings = mappings;
                    node->num_port_mappings = num_mappings;
                    node->port_mappings_cap = num_mappings;
                }
                else
                {
                    free(mappings);
                }
                for (int cx = x; cx <= end; cx++)
                {
                    gm[y][cx] = ctx->gate_idx;
                }
                ctx->gate_idx++;
                free(raw);
                free(name);
                continue;
            }

            free(mappings);

            /* Unknown */
            if (ctx->err)
            {
                error_set_msg(ctx->err,
                              "unknown gate or subcircuit [%s] at "
                              "(%d,%d)",
                              name, x, y);
            }
            free(raw);
            free(name);
        }
    }

done:
    return;
}

/* ================================================================
   PASS 2: Find I/O ports
   ================================================================ */

typedef struct
{
    char name[64];
    int x, y;
    int is_in, is_out;
    int width;
} port_cand_t;

static void pass_find_ports(parse_ctx_t *ctx)
{
    int W = ctx->W, H = ctx->H;
    grid_t *g = ctx->g;
    int **gm = ctx->gate_map;
    netlist_t *nl = ctx->nl;

    port_cand_t *cands = NULL;
    int nc = 0, cands_cap = 0;

    for (int y = 0; y < H; y++)
    {
        int x = 0;
        while (x < W)
        {
            if (!port_start(grid_get(g, x, y)) || gm[y][x] >= 0)
            {
                x++;
                continue;
            }
            int s = x;
            while (x < W && port_cont(grid_get(g, x, y)))
            {
                x++;
            }
            int plen = x - s;

            /* Signal names must fit a node name (NAME_MAX). */
            if (plen >= 64)
            {
                if (ctx->err)
                {
                    char nm[64];
                    strncpy(nm, &g->data[y][s], 63);
                    nm[63] = '\0';
                    error_set_msg(ctx->err,
                                  "signal name '%s...' at (%d,%d) is too "
                                  "long (max 63)",
                                  nm, s, y);
                }
                goto done;
            }

            char pn[64];
            strncpy(pn, &g->data[y][s], plen);
            pn[plen] = '\0';

            /* Optional bus range: NAME[N:M] -> width |N-M|+1, value bit 0
               is the lowest bit index. */
            int width = 1, blen = 0, hi = 0, lo = 0;
            if (grid_get(g, x, y) == '[')
            {
                int consumed = 0;
                char *p = &g->data[y][x] + 1;
                char *end = NULL;
                long v1 = strtol(p, &end, 10);
                if (end != p && *end == ':')
                {
                    p = end + 1;
                    long v2 = strtol(p, &end, 10);
                    if (end != p && *end == ']')
                    {
                        hi = (int)v1;
                        lo = (int)v2;
                        consumed = (int)(end - &g->data[y][x]) + 1;
                        width = (hi >= lo) ? (hi - lo + 1) : (lo - hi + 1);
                        if (width > 16)
                        {
                            if (ctx->err)
                            {
                                error_set_msg(ctx->err,
                                              "bus '%s[%d:%d]' at (%d,%d) is "
                                              "wider than 16 bits",
                                              pn, hi, lo, s, y);
                            }
                            goto done;
                        }
                        blen = consumed;
                        x += consumed;
                    }
                }
            }

            /* Port markers: A> = input port, Q< = output port */
            int is_in = 0, is_out = 0;
            int extent = plen + blen;
            char mk = grid_get(g, x, y);
            if (mk == '>')
            {
                is_in = 1;
                extent = plen + blen + 1;
                if (ctx->covered)
                {
                    ctx->covered[y * W + x] = 1; /* marker part of label */
                }
                x++;
            }
            else if (mk == '<')
            {
                is_out = 1;
                extent = plen + blen + 1;
                if (ctx->covered)
                {
                    ctx->covered[y * W + x] = 1;
                }
                x++;
            }

            if (!has_adjacent_wire(g, s, y, extent))
            {
                continue;
            }

            /* Mark every cell of this bus label so the BFS resolves the
               node — for every occurrence, not just the first. */
            if (ctx->bus_cell && width > 1)
            {
                node_t *bn = netlist_find_node(nl, pn);
                if (bn)
                {
                    for (int c = 0; c < plen + blen; c++)
                    {
                        ctx->bus_cell[y][s + c] = bn->id;
                    }
                }
            }

            /* The same name may appear multiple times as long as the
               marker and width agree — it is the same signal drawn twice. */
            int dup = 0, conflict = 0;
            for (int i = 0; i < nc; i++)
            {
                if (strcmp(cands[i].name, pn) != 0)
                {
                    continue;
                }
                dup = 1;
                if (cands[i].is_in != is_in || cands[i].is_out != is_out)
                {
                    conflict = 1;
                }
                else if (cands[i].width != width)
                {
                    conflict = 1;
                }
                break;
            }
            if (conflict)
            {
                if (ctx->err)
                {
                    error_set_msg(ctx->err,
                                  "signal '%s' at (%d,%d) conflicts with a "
                                  "different port marker or width",
                                  pn, s, y);
                }
                goto done;
            }
            if (dup)
            {
                continue;
            }

            node_type_t nt = is_out ? NODE_OUTPUT : NODE_INPUT;
            node_t *node = netlist_add_node(nl, pn, nt, s, y);
            node->is_public = (is_in || is_out) ? 1 : 0;
            node->width = width;
            if (blen)
            {
                node->bus_msb = hi >= lo ? hi : lo;
                node->bus_lsb = hi >= lo ? lo : hi;
            }
            /* First occurrence: mark cells (duplicates handled above). */
            if (ctx->bus_cell && width > 1)
            {
                for (int c = 0; c < plen + blen; c++)
                {
                    ctx->bus_cell[y][s + c] = node->id;
                }
            }
            if (nc >= cands_cap)
            {
                cands_cap = cands_cap ? cands_cap * 2 : 64;
                port_cand_t *cd = realloc(cands, (size_t)cands_cap * sizeof(port_cand_t));
                if (!cd)
                {
                    goto done;
                }
                cands = cd;
            }
            strncpy(cands[nc].name, pn, 63);
            cands[nc].is_in = is_in;
            cands[nc].is_out = is_out;
            cands[nc].width = width;
            cands[nc].x = s;
            cands[nc].y = y;
            nc++;
        }
    }

done:
    free(cands);
}

/* ================================================================
   PASS 3: Trace wires (BFS from each gate port)
   ================================================================ */

/* A signal feeds a gate: propagate width. An internal label adopts the
   width of the gate that drives it; a width-1 signal feeding a bus gate's
   data pin is broadcast (replicated). Returns -1 on mismatch. */
static int absorb_width(parse_ctx_t *ctx, node_t *gate, int pin, node_t *sig)
{
    if (gate->type == NODE_INPUT || gate->type == NODE_OUTPUT || gate->type == NODE_SUBCIRCUIT)
    {
        return 0;
    }
    int is_clock = 0;
    if (gate->type == NODE_DFF || gate->type == NODE_DLATCH)
    {
        is_clock = (pin == 1);
    }
    if (gate->type == NODE_JKFF)
    {
        is_clock = (pin == 2);
    }
    if (is_clock)
    {
        if (sig->width != 1)
        {
            if (ctx->err)
            {
                error_set_msg(ctx->err,
                              "clock input of [%s] at (%d,%d) must be "
                              "scalar, not %d bits wide",
                              gate->name, gate->x, gate->y, sig->width);
            }
            return -1;
        }
        return 0;
    }
    if (gate->width == 0)
    {
        /* A bus source fixes the gate's width; a scalar source does not
           lock it, so a scalar D can broadcast to a bus Q (sequential)
           or a scalar select to a bus mux. */
        if (sig->width > 1)
        {
            gate->width = sig->width;
            gate->bus_msb = gate->width - 1;
            gate->bus_lsb = 0;
        }
        return 0;
    }
    /* Output side: a gate drives an internal label, which adopts the
       gate's width (it is a name for the net). */
    if (pin < 0 && !sig->is_public && sig->width == 1 && gate->width > 1)
    {
        sig->width = gate->width;
        sig->bus_msb = gate->width - 1;
        sig->bus_lsb = 0;
        return 0;
    }
    /* Input side: a width-1 signal feeding a bus gate's data pin is
       broadcast, so this is legal (gate keeps its width). */
    if (pin >= 0 && gate->width > 1 && sig->width == 1)
    {
        return 0;
    }
    if (gate->width != sig->width)
    {
        if (ctx->err)
        {
            error_set_msg(ctx->err,
                          "[%s] at (%d,%d) mixes %d-bit and %d-bit signals "
                          "(width mismatch)",
                          gate->name, gate->x, gate->y, gate->width, sig->width);
        }
        return -1;
    }
    return 0;
}

static void add_binding(node_t *node, int is_out, const char *name, node_t *external)
{
    if (is_out)
    {
        if (node->num_output_bindings >= node->output_bindings_cap)
        {
            node->output_bindings_cap =
                node->output_bindings_cap ? node->output_bindings_cap * 2 : 4;
            port_binding_t *nb = realloc(node->output_bindings, (size_t)node->output_bindings_cap *
                                                                    sizeof(port_binding_t));
            if (!nb)
            {
                return;
            }
            node->output_bindings = nb;
        }
        // NOLINTBEGIN: realloc guarded above
        strncpy(node->output_bindings[node->num_output_bindings].name, name, 63);
        // NOLINTEND
        node->output_bindings[node->num_output_bindings].external_node = external;
        node->num_output_bindings++;
    }
    else
    {
        if (node->num_input_bindings >= node->input_bindings_cap)
        {
            node->input_bindings_cap = node->input_bindings_cap ? node->input_bindings_cap * 2 : 4;
            port_binding_t *nb = realloc(node->input_bindings,
                                         (size_t)node->input_bindings_cap * sizeof(port_binding_t));
            if (!nb)
            {
                return;
            }
            node->input_bindings = nb;
        }
        strncpy(node->input_bindings[node->num_input_bindings].name, name, 63);
        node->input_bindings[node->num_input_bindings].external_node = external;
        node->num_input_bindings++;
    }
}

static void pass_trace_wires(parse_ctx_t *ctx)
{
    int W = ctx->W, H = ctx->H;
    grid_t *g = ctx->g;
    int **gm = ctx->gate_map;
    netlist_t *nl = ctx->nl;

    int *visited = arena_alloc(&ctx->arena, (size_t)H * W * sizeof(int));
    memset(visited, 0, (size_t)H * W * sizeof(int));

    for (int ni = 0; ni < nl->num_nodes; ni++)
    {
        node_t *node = nl->nodes[ni];
        node_type_t nt = node->type;

        if (nt == NODE_GND || nt == NODE_VCC || nt == NODE_INPUT || nt == NODE_OUTPUT)
        {
            continue;
        }

        int gx = node->x, gy = node->y;

        /* Compute gate width */
        int gw = 0;
        while (grid_in_bounds(g, gx + gw, gy) && grid_get(g, gx + gw, gy) != ']')
        {
            gw++;
        }
        gw++;

        /* Determine number of input pins */
        int num_pins = (nt == NODE_SUBCIRCUIT)
                           ? 1
                           : (node->num_inputs_needed > 0 ? node->num_inputs_needed : 1);

        /* Build port list */
        int pin_of[16], px[16], py[16], is_out[16], np = 0;
        for (int p = 0; p < num_pins; p++)
        {
            pin_of[np] = p;
            px[np] = (p == 0) ? (gx - 1) : (p == 1 ? gx : gx + p - 1);
            py[np] = gy + p;
            is_out[np] = 0;
            np++;
        }
        pin_of[np] = 0;
        px[np] = gx + gw;
        py[np] = gy;
        is_out[np] = 1;
        np++;

        /* BFS from each port */
        for (int p = 0; p < np; p++)
        {
            int sx = px[p], sy = py[p];
            int output_port = is_out[p];
            int this_pin = pin_of[p];

            if (!grid_in_bounds(g, sx, sy))
            {
                continue;
            }
            char sc = grid_get(g, sx, sy);
            if (sc != '-' && sc != '|' && sc != '+' && sc != '[' && sc != ']' &&
                !isalnum((unsigned char)sc) && sc != '_')
            {
                continue;
            }

            int base_visit = ni * 100 + p + 1;
            int qsize = W * H * 2;
            int *q = arena_alloc(&ctx->arena, (size_t)qsize * sizeof(int));
            int head = 0, tail = 0;
            q[tail++] = sx;
            q[tail++] = sy;
            visited[sy * W + sx] = base_visit;

            while (head < tail)
            {
                int cx = q[head++], cy = q[head++];
                if (ctx->covered)
                {
                    ctx->covered[cy * W + cx] = 1;
                }
                int skip_nb = 0;

                if ((cx != sx || cy != sy))
                {
                    char cc = grid_get(g, cx, cy);
                    int gmv = gm[cy][cx];

                    /* Gate cell */
                    if (gmv >= 0)
                    {
                        node_t *gn = nl->nodes[gmv];
                        if (gn != node && output_port)
                        {
                            netlist_add_wire(nl, node, 0, gn, 0);
                            if (absorb_width(ctx, gn, 0, node) < 0)
                            {
                                return;
                            }
                        }
                        skip_nb = 1;
                    }

                    /* Bus label cell: resolve straight to the bus node. */
                    if (!skip_nb && ctx->bus_cell && ctx->bus_cell[cy][cx] >= 0)
                    {
                        if (getenv("CIRCTRACE"))
                        {
                            fprintf(stderr,
                                    "  BUS node=%s pin=%d out=%d cell=(%d,%d) -> %s width=%d\n",
                                    node->name, this_pin, output_port, cx, cy,
                                    nl->nodes[ctx->bus_cell[cy][cx]]->name,
                                    nl->nodes[ctx->bus_cell[cy][cx]]->width);
                        }
                        node_t *bn = nl->nodes[ctx->bus_cell[cy][cx]];
                        if (bn != node)
                        {
                            if (nt == NODE_SUBCIRCUIT)
                            {
                                if (ctx->err)
                                {
                                    error_set_msg(ctx->err,
                                                  "subcircuit '%s' at (%d,%d) "
                                                  "is connected to a bus "
                                                  "'%s'; buses are not "
                                                  "supported on subcircuits "
                                                  "yet",
                                                  node->name, node->x, node->y, bn->name);
                                }
                                return;
                            }
                            if (output_port)
                            {
                                if (absorb_width(ctx, node, -1, bn) < 0)
                                {
                                    return;
                                }
                                netlist_add_wire(nl, node, this_pin, bn, 0);
                            }
                            else
                            {
                                if (absorb_width(ctx, node, this_pin, bn) < 0)
                                {
                                    return;
                                }
                                netlist_add_wire(nl, bn, 0, node, this_pin);
                            }
                        }
                        skip_nb = 1;
                    }

                    /* Port cell */
                    if (!skip_nb && (isalnum((unsigned char)cc) || cc == '_'))
                    {
                        char pname[64];
                        extract_port_name(g, cx, cy, pname, sizeof(pname));

                        /* Directional port connection for 3+ pin gates (JKFF):
                           Pin 0 = signals from above (J), pins 1+ = from below (K,CLK) */
                        if (!output_port && nt != NODE_SUBCIRCUIT && num_pins > 2)
                        {
                            if (this_pin == 0 && cy > gy)
                            {
                                continue;
                            }
                            if (this_pin != 0 && cy <= gy)
                            {
                                continue;
                            }
                        }

                        node_t *pn = netlist_find_node(nl, pname);
                        if (pn && pn != node)
                        {
                            if (nt == NODE_SUBCIRCUIT)
                            {
                                char spn[64];
                                strncpy(spn, pname, 63);
                                spn[63] = '\0';
                                for (int mi = 0; mi < node->num_port_mappings; mi++)
                                {
                                    if (strcmp(node->port_mappings[mi].external_name, pname) == 0)
                                    {
                                        strncpy(spn, node->port_mappings[mi].sub_port_name, 63);
                                        break;
                                    }
                                }
                                const char *bn = (node->num_port_mappings > 0) ? spn : pname;
                                node_t *sp = netlist_find_port(node->sub_netlist, spn);
                                if (sp && ((output_port && sp->type == NODE_OUTPUT) ||
                                           (!output_port && sp->type == NODE_INPUT)))
                                {
                                    add_binding(node, output_port, bn, pn);
                                }
                            }
                            else
                            {
                                if (output_port)
                                {
                                    if (absorb_width(ctx, node, -1, pn) < 0)
                                    {
                                        return;
                                    }
                                    netlist_add_wire(nl, node, this_pin, pn, 0);
                                }
                                else
                                {
                                    if (absorb_width(ctx, node, this_pin, pn) < 0)
                                    {
                                        return;
                                    }
                                    netlist_add_wire(nl, pn, 0, node, this_pin);
                                }
                                skip_nb = 1;
                            }
                        }
                    }
                }

                if (!skip_nb)
                {
                    static const int DX[] = {-1, 1, 0, 0};
                    static const int DY[] = {0, 0, -1, 1};
                    char cur = grid_get(g, cx, cy);
                    int cur_gm = gm[cy][cx];
                    int c1 = cell_connections(cur, cur_gm >= 0);
                    for (int d = 0; d < 4; d++)
                    {
                        int nx = cx + DX[d], ny = cy + DY[d];
                        if (!grid_in_bounds(g, nx, ny))
                        {
                            continue;
                        }
                        int vi = ny * W + nx;
                        if (visited[vi] == base_visit)
                        {
                            continue;
                        }
                        char nc = grid_get(g, nx, ny);
                        int c2 = cell_connections(nc, 0);
                        int ok = 0;
                        int dx = nx - cx, dy = ny - cy;
                        if (dx == 1 && dy == 0)
                        {
                            ok = (c1 & CONN_R) && (c2 & CONN_L);
                        }
                        else if (dx == -1 && dy == 0)
                        {
                            ok = (c1 & CONN_L) && (c2 & CONN_R);
                        }
                        else if (dx == 0 && dy == 1)
                        {
                            ok = (c1 & CONN_D) && (c2 & CONN_U);
                        }
                        else if (dx == 0 && dy == -1)
                        {
                            ok = (c1 & CONN_U) && (c2 & CONN_D);
                        }
                        if (!ok)
                        {
                            continue;
                        }
                        visited[vi] = base_visit;
                        q[tail++] = nx;
                        q[tail++] = ny;
                    }
                }
            }
        }
    }
}

/* ================================================================
   PASS 3.5: Warn about wire characters no net ever reaches
   ================================================================ */

static void pass_warn_stray(parse_ctx_t *ctx)
{
    if (!ctx->covered)
    {
        return;
    }
    int W = ctx->W, H = ctx->H;
    grid_t *g = ctx->g;
    int warned = 0;
    for (int y = 0; y < H; y++)
    {
        for (int x = 0; x < W; x++)
        {
            char c = grid_get(g, x, y);
            if (c != '-' && c != '|' && c != '+' && c != '>' && c != '<')
            {
                continue;
            }
            if (ctx->covered[y * W + x])
            {
                continue;
            }
            if (!warned)
            {
                fprintf(stderr, "warning: stray wire characters not connected to "
                                "anything:\n");
                warned = 1;
            }
            fprintf(stderr, "  (%d,%d) '%c'\n", x, y, c);
        }
    }
}

/* ================================================================
   PASS 4: Auto-bind unmapped subcircuit output ports
   ================================================================ */

static void pass_resolve_bindings(parse_ctx_t *ctx)
{
    netlist_t *nl = ctx->nl;
    for (int si = 0; si < nl->num_nodes; si++)
    {
        node_t *sn = nl->nodes[si];
        if (sn->type != NODE_SUBCIRCUIT || !sn->sub_netlist)
        {
            continue;
        }
        netlist_t *sub = sn->sub_netlist;
        for (int mi = 0; mi < sn->num_port_mappings; mi++)
        {
            port_mapping_t *pm = &sn->port_mappings[mi];
            node_t *sp = netlist_find_port(sub, pm->sub_port_name);
            if (!sp || sp->type != NODE_OUTPUT)
            {
                continue;
            }
            int already = 0;
            for (int b = 0; b < sn->num_output_bindings; b++)
            {
                if (strcmp(sn->output_bindings[b].name, pm->sub_port_name) == 0)
                {
                    already = 1;
                    break;
                }
            }
            if (already)
            {
                continue;
            }
            node_t *ext = netlist_find_node(nl, pm->external_name);
            if (ext)
            {
                add_binding(sn, 1, pm->sub_port_name, ext);
            }
        }
    }
}

/* ================================================================
   PASS 5: Fixup port types (INPUT→OUTPUT)
   ================================================================ */

static void pass_fixup_types(parse_ctx_t *ctx)
{
    netlist_t *nl = ctx->nl;
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        if (n->type == NODE_INPUT && !n->is_public && n->num_inputs > 0)
        {
            n->type = NODE_OUTPUT;
        }
    }
    /* Gates with no bus-connected net are scalar (width 1). */
    for (int i = 0; i < nl->num_nodes; i++)
    {
        if (nl->nodes[i]->width == 0)
        {
            nl->nodes[i]->width = 1;
        }
    }
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        if (n->type != NODE_SUBCIRCUIT)
        {
            continue;
        }
        for (int j = 0; j < n->num_output_bindings; j++)
        {
            if (n->output_bindings[j].external_node &&
                n->output_bindings[j].external_node->type == NODE_INPUT &&
                !n->output_bindings[j].external_node->is_public)
            {
                n->output_bindings[j].external_node->type = NODE_OUTPUT;
            }
        }
    }
    /* Dedup gate inputs: same source connected to same gate via different pins */
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        if (n->type == NODE_INPUT || n->type == NODE_OUTPUT)
        {
            continue;
        }
        for (int j = 0; j < n->num_inputs; j++)
        {
            node_t *src = n->inputs[j]->src;
            for (int k = j + 1; k < n->num_inputs; k++)
            {
                if (n->inputs[k]->src == src)
                {
                    /* Remove duplicate */
                    wire_t *w = n->inputs[k];
                    /* Remove from src's outputs */
                    for (int o = 0; o < src->num_outputs; o++)
                    {
                        if (src->outputs[o] == w)
                        {
                            memmove(&src->outputs[o], &src->outputs[o + 1],
                                    (src->num_outputs - o - 1) * sizeof(wire_t *));
                            src->num_outputs--;
                            break;
                        }
                    }
                    free(w);
                    memmove(&n->inputs[k], &n->inputs[k + 1],
                            (n->num_inputs - k - 1) * sizeof(wire_t *));
                    n->num_inputs--;
                    k--;
                }
            }
        }
    }
}

/* ================================================================
   VALIDATION: turn silent miswiring into hard errors
   ================================================================ */

static int is_loop_barrier(node_t *n)
{
    node_type_t t = n->type;
    if (t == NODE_DFF || t == NODE_JKFF || t == NODE_DLATCH || t == NODE_GND || t == NODE_VCC ||
        t == NODE_CLOCK)
    {
        return 1;
    }
    /* A subcircuit with no sequential internals is transparent, so
       combinational loops through it are real and must be rejected. */
    if (t == NODE_SUBCIRCUIT)
    {
        return n->sub_netlist ? netlist_has_sequential(n->sub_netlist) : 1;
    }
    return 0;
}

static int validate_loop_dfs(parse_ctx_t *ctx, netlist_t *nl, node_t *n, int *color)
{
    color[n->id] = 1; /* GRAY */
    if (!is_loop_barrier(n))
    {
        for (int j = 0; j < n->num_inputs; j++)
        {
            node_t *src = n->inputs[j]->src;
            if (color[src->id] == 1)
            {
                if (ctx->err)
                {
                    error_set_msg(ctx->err, "combinational loop detected involving '%s'",
                                  src->name);
                }
                return -1;
            }
            if (color[src->id] == 0 && validate_loop_dfs(ctx, nl, src, color) < 0)
            {
                return -1;
            }
        }
        /* A transparent subcircuit depends on its input bindings, and an
           output label driven by a sub output binding depends on the sub.
           Following these closes loops that run through subcircuits. */
        if (n->type == NODE_SUBCIRCUIT)
        {
            for (int b = 0; b < n->num_input_bindings; b++)
            {
                node_t *e = n->input_bindings[b].external_node;
                if (!e)
                {
                    continue;
                }
                if (color[e->id] == 1)
                {
                    if (ctx->err)
                    {
                        error_set_msg(ctx->err,
                                      "combinational loop detected "
                                      "involving '%s'",
                                      e->name);
                    }
                    return -1;
                }
                if (color[e->id] == 0 && validate_loop_dfs(ctx, nl, e, color) < 0)
                {
                    return -1;
                }
            }
        }
        else if (n->type == NODE_OUTPUT && n->num_inputs == 0)
        {
            for (int i = 0; i < nl->num_nodes; i++)
            {
                node_t *s = nl->nodes[i];
                if (s->type != NODE_SUBCIRCUIT || is_loop_barrier(s))
                {
                    continue;
                }
                for (int b = 0; b < s->num_output_bindings; b++)
                {
                    if (s->output_bindings[b].external_node == n)
                    {
                        if (color[s->id] == 1)
                        {
                            if (ctx->err)
                            {
                                error_set_msg(ctx->err,
                                              "combinational loop detected "
                                              "involving '%s'",
                                              n->name);
                            }
                            return -1;
                        }
                        if (color[s->id] == 0 && validate_loop_dfs(ctx, nl, s, color) < 0)
                        {
                            return -1;
                        }
                    }
                }
            }
        }
    }
    color[n->id] = 2; /* BLACK */
    return 0;
}

static int validate_netlist(parse_ctx_t *ctx)
{
    netlist_t *nl = ctx->nl;
    circ_error_t *err = ctx->err;
    int rc = -1;

    /* 1. Duplicate names: two ports/labels/subcircuits with one name is
       ambiguous; a port or subcircuit sharing a gate's name is too. */
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *a = nl->nodes[i];
        for (int j = i + 1; j < nl->num_nodes; j++)
        {
            node_t *b = nl->nodes[j];
            if (strcmp(a->name, b->name) != 0)
            {
                continue;
            }
            int a_port = (a->type == NODE_INPUT || a->type == NODE_OUTPUT);
            int b_port = (b->type == NODE_INPUT || b->type == NODE_OUTPUT);
            if (a_port || a->type == NODE_SUBCIRCUIT || b_port || b->type == NODE_SUBCIRCUIT)
            {
                if (err)
                {
                    error_set_msg(err, "duplicate name '%s'", a->name);
                }
                goto done;
            }
        }
    }

    /* 1.5. Wire widths must match on both ends (clock pins of sequential
       gates are scalar by design; a width-1 signal feeding a bus gate's
       data pin is broadcast). */
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        for (int j = 0; j < n->num_inputs; j++)
        {
            node_t *src = n->inputs[j]->src;
            int pin = n->inputs[j]->dst_pin;
            int is_clk = ((n->type == NODE_DFF || n->type == NODE_DLATCH) && pin == 1) ||
                         (n->type == NODE_JKFF && pin == 2);
            if (is_clk)
            {
                continue;
            }
            int is_broadcast = (n->type != NODE_INPUT && n->type != NODE_OUTPUT &&
                                n->type != NODE_SUBCIRCUIT && src->width == 1 && n->width > 1);
            if (is_broadcast)
            {
                continue;
            }
            if (src->width != n->width)
            {
                if (err)
                {
                    error_set_msg(err,
                                  "'%s' (%d bits) connects to '%s' (%d bits) "
                                  "- width mismatch",
                                  src->name, src->width, n->name, n->width);
                }
                goto done;
            }
        }
    }

    /* 2. Gate inputs must be driven.
       Symmetric combinational gates fold any number of labels that merge
       onto their sockets (junction fan-in), so only a minimum count is
       required. Sequential gates have positional pins (D/CLK, J/K/CLK),
       so every pin must be driven. */
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        if (n->type == NODE_INPUT || n->type == NODE_OUTPUT || n->type == NODE_GND ||
            n->type == NODE_VCC || n->type == NODE_CLOCK || n->type == NODE_SUBCIRCUIT)
        {
            continue;
        }
        if (n->num_inputs_needed == 0)
        {
            continue;
        }
        if (n->type == NODE_DFF || n->type == NODE_JKFF || n->type == NODE_DLATCH)
        {
            /* Sequential pins are positional (D/CLK, J/K/CLK, D/EN) and
               must each be driven by exactly one net — a merged junction
               here would silently pick the wrong input. */
            for (int p = 0; p < n->num_inputs_needed; p++)
            {
                int count = 0;
                for (int j = 0; j < n->num_inputs; j++)
                {
                    if (n->inputs[j]->dst_pin == p)
                    {
                        count++;
                    }
                }
                if (count == 0)
                {
                    if (err)
                    {
                        error_set_msg(err,
                                      "input pin %d of [%s] at (%d,%d) is "
                                      "not connected",
                                      p, n->name, n->x, n->y);
                    }
                    goto done;
                }
                if (count > 1)
                {
                    if (err)
                    {
                        error_set_msg(err,
                                      "input pin %d of [%s] at (%d,%d) is "
                                      "driven by %d signals (short)",
                                      p, n->name, n->x, n->y, count);
                    }
                    goto done;
                }
            }
        }
        else if (n->num_inputs < n->num_inputs_needed)
        {
            if (err)
            {
                error_set_msg(err,
                              "[%s] at (%d,%d) has only %d of %d inputs "
                              "connected",
                              n->name, n->x, n->y, n->num_inputs, n->num_inputs_needed);
            }
            goto done;
        }
    }

    /* 3. Port/label sanity. A label is "driven" by a wire or by a
       subcircuit output binding; "consumed" by a wire or a subcircuit
       input binding. */
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        if (n->type != NODE_INPUT && n->type != NODE_OUTPUT)
        {
            continue;
        }
        /* Count distinct drivers: wires into the label, plus distinct
           subcircuits whose output binding targets it. */
        int nsub_drivers = 0, consumed_by_sub = 0;
        for (int k = 0; k < nl->num_nodes; k++)
        {
            node_t *s = nl->nodes[k];
            if (s->type != NODE_SUBCIRCUIT)
            {
                continue;
            }
            int binds = 0;
            for (int b = 0; b < s->num_output_bindings; b++)
            {
                if (s->output_bindings[b].external_node == n)
                {
                    binds = 1;
                }
            }
            nsub_drivers += binds;
            for (int b = 0; b < s->num_input_bindings; b++)
            {
                if (s->input_bindings[b].external_node == n)
                {
                    consumed_by_sub = 1;
                }
            }
        }
        int drivers = n->num_inputs + nsub_drivers;
        int driven = drivers > 0;
        int consumed = n->num_outputs > 0 || consumed_by_sub;

        if (n->is_public)
        {
            if (n->type == NODE_INPUT)
            {
                if (driven)
                {
                    if (err)
                    {
                        error_set_msg(err,
                                      "input port '%s' at (%d,%d) is driven "
                                      "by a signal (short)",
                                      n->name, n->x, n->y);
                    }
                    goto done;
                }
                if (!consumed)
                {
                    if (err)
                    {
                        error_set_msg(err,
                                      "input port '%s' at (%d,%d) is not "
                                      "connected to anything",
                                      n->name, n->x, n->y);
                    }
                    goto done;
                }
            }
            else
            {
                if (drivers == 0)
                {
                    if (err)
                    {
                        error_set_msg(err,
                                      "output port '%s' at (%d,%d) has no "
                                      "driver",
                                      n->name, n->x, n->y);
                    }
                    goto done;
                }
                if (drivers > 1)
                {
                    if (err)
                    {
                        error_set_msg(err,
                                      "output port '%s' at (%d,%d) is driven "
                                      "by multiple signals (short)",
                                      n->name, n->x, n->y);
                    }
                    goto done;
                }
            }
        }
        else
        {
            if (drivers == 0)
            {
                if (err)
                {
                    error_set_msg(err, "label '%s' at (%d,%d) has no driver", n->name, n->x, n->y);
                }
                goto done;
            }
            if (drivers > 1)
            {
                if (err)
                {
                    error_set_msg(err,
                                  "label '%s' at (%d,%d) is driven by "
                                  "multiple sources",
                                  n->name, n->x, n->y);
                }
                goto done;
            }
            if (!consumed)
            {
                if (err)
                {
                    error_set_msg(err, "label '%s' at (%d,%d) is never used", n->name, n->x, n->y);
                }
                goto done;
            }
        }
    }

    /* 4. Every public input port of a subcircuit must be connected, either
       via an explicit binding or (for the first port) a direct wire from
       another gate's output socket. A port bound to two different external
       signals (a merged net with several matching labels) is ambiguous. */
    for (int i = 0; i < nl->num_nodes; i++)
    {
        node_t *n = nl->nodes[i];
        if (n->type != NODE_SUBCIRCUIT || !n->sub_netlist)
        {
            continue;
        }
        /* Port mappings must name real ports of the subcircuit. */
        for (int m = 0; m < n->num_port_mappings; m++)
        {
            if (!netlist_find_port(n->sub_netlist, n->port_mappings[m].sub_port_name))
            {
                if (err)
                {
                    error_set_msg(err,
                                  "subcircuit '%s' at (%d,%d): mapping "
                                  "references unknown port '%s'",
                                  n->name, n->x, n->y, n->port_mappings[m].sub_port_name);
                }
                goto done;
            }
        }
        for (int b1 = 0; b1 < n->num_input_bindings; b1++)
        {
            node_t *e1 = n->input_bindings[b1].external_node;
            for (int b2 = b1 + 1; b2 < n->num_input_bindings; b2++)
            {
                node_t *e2 = n->input_bindings[b2].external_node;
                if (e1 && e2 && e1 != e2 &&
                    strcmp(n->input_bindings[b1].name, n->input_bindings[b2].name) == 0)
                {
                    if (err)
                    {
                        error_set_msg(err,
                                      "subcircuit '%s' at (%d,%d): input "
                                      "port '%s' is bound to both '%s' and "
                                      "'%s' (ambiguous)",
                                      n->name, n->x, n->y, n->input_bindings[b1].name, e1->name,
                                      e2->name);
                    }
                    goto done;
                }
            }
        }
        int idx = 0;
        for (int j = 0; j < n->sub_netlist->num_nodes; j++)
        {
            node_t *sp = n->sub_netlist->nodes[j];
            if (sp->type != NODE_INPUT || !sp->is_public)
            {
                continue;
            }
            int connected = 0;
            for (int b = 0; b < n->num_input_bindings; b++)
            {
                if (strcmp(n->input_bindings[b].name, sp->name) == 0)
                {
                    connected = 1;
                    break;
                }
            }
            if (!connected && idx == 0 && n->num_inputs > 0)
            {
                connected = 1;
            }
            if (!connected)
            {
                if (err)
                {
                    error_set_msg(err,
                                  "subcircuit '%s' at (%d,%d): input port "
                                  "'%s' is not connected",
                                  n->name, n->x, n->y, sp->name);
                }
                goto done;
            }
            idx++;
        }
    }

    /* 5. Combinational loop detection (sequential + subcircuits are
       barriers). */
    {
        int *color = arena_alloc(&ctx->arena, (size_t)nl->num_nodes * sizeof(int));
        memset(color, 0, (size_t)nl->num_nodes * sizeof(int));
        for (int i = 0; i < nl->num_nodes; i++)
        {
            if (color[nl->nodes[i]->id] == 0 && validate_loop_dfs(ctx, nl, nl->nodes[i], color) < 0)
            {
                goto done;
            }
        }
    }

    rc = 0;
done:
    return rc;
}

/* ================================================================
   Public API
   ================================================================ */

netlist_t *parse_circuit_er(grid_t *g, const char *dir, circ_error_t *err)
{
    parse_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.g = g;
    ctx.W = g->width;
    ctx.H = g->height;
    ctx.nl = netlist_create();
    ctx.err = err;
    if (dir)
    {
        strncpy(ctx.search_dir, dir, 511);
    }

    arena_init(&ctx.arena);

    /* Gate map: which gate occupies each cell (-1 = none) */
    ctx.gate_map = arena_alloc(&ctx.arena, ctx.H * sizeof(int *));
    ctx.bus_cell = arena_alloc(&ctx.arena, ctx.H * sizeof(int *));
    for (int y = 0; y < ctx.H; y++)
    {
        ctx.gate_map[y] = arena_alloc(&ctx.arena, ctx.W * sizeof(int));
        ctx.bus_cell[y] = arena_alloc(&ctx.arena, ctx.W * sizeof(int));
        for (int x = 0; x < ctx.W; x++)
        {
            ctx.gate_map[y][x] = -1;
            ctx.bus_cell[y][x] = -1;
        }
    }

    /* Covered map: cells reached by any wire BFS (for stray-wire warns) */
    ctx.covered = arena_alloc(&ctx.arena, (size_t)ctx.H * ctx.W);
    memset(ctx.covered, 0, (size_t)ctx.H * ctx.W);

    /* Run the pipeline */
    pass_reject_diagonals(&ctx);
    if (err && err->msg[0])
    {
        goto fail;
    }

    pass_find_gates(&ctx);
    if (err && err->msg[0])
    {
        goto fail;
    }

    pass_find_ports(&ctx);
    if (err && err->msg[0])
    {
        goto fail;
    }

    pass_trace_wires(&ctx);
    if (err && err->msg[0])
    {
        goto fail;
    }

    pass_warn_stray(&ctx);

    pass_resolve_bindings(&ctx);

    pass_fixup_types(&ctx);

    if (validate_netlist(&ctx) != 0 && err && err->msg[0])
    {
        goto fail;
    }

    if (err)
    {
        error_clear(err);
    }
    arena_free_all(&ctx.arena);
    return ctx.nl;

fail:
    netlist_free(ctx.nl);
    arena_free_all(&ctx.arena);
    return NULL;
}

netlist_t *parse_circuit_with_dir(grid_t *g, const char *dir)
{
    circ_error_t err;
    error_clear(&err);
    return parse_circuit_er(g, dir, &err);
}

netlist_t *parse_circuit(grid_t *g)
{
    return parse_circuit_with_dir(g, NULL);
}

void parse_debug_grid(grid_t *g)
{
    fprintf(stderr, "Grid %dx%d:\n", g->width, g->height);
    for (int y = 0; y < g->height; y++)
    {
        fprintf(stderr, "  y=%d: '", y);
        for (int x = 0; x < g->width; x++)
        {
            fputc(grid_get(g, x, y), stderr);
        }
        fprintf(stderr, "'\n");
    }
}
