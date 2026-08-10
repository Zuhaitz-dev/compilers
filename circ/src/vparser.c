#include "vparser.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---- Line-based Verilog parser (limited subset) ---- */

/* Trim whitespace from both ends of a string in-place */
static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t')
    {
        s++;
    }
    char *end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n'))
    {
        end--;
    }
    *(end + 1) = '\0';
    return s;
}

/* Find signal by name, return index or -1 */
static int find_signal(vmodule_t *mod, const char *name)
{
    for (int i = 0; i < mod->num_signals; i++)
    {
        if (strcmp(mod->signals[i].name, name) == 0)
        {
            return i;
        }
    }
    return -1;
}

/* Add a signal if it doesn't already exist */
static int add_signal(vmodule_t *mod, const char *name, int is_input, int is_output)
{
    int idx = find_signal(mod, name);
    if (idx >= 0)
    {
        vsignal_t *sig = &mod->signals[idx];
        if (is_input)
        {
            sig->is_input = 1;
        }
        if (is_output)
        {
            sig->is_output = 1;
        }
        return idx;
    }
    if (mod->num_signals >= MAX_SIGNALS)
    {
        fprintf(stderr, "Too many signals\n");
        return -1;
    }
    int i = mod->num_signals++;
    strncpy(mod->signals[i].name, name, 63);
    mod->signals[i].is_input = is_input;
    mod->signals[i].is_output = is_output;
    return i;
}

/* Parse a comma-separated list of identifiers from a string.
   Advances the pointer past the list. */
static void parse_id_list(const char **pp, vmodule_t *mod, int is_input, int is_output)
{
    const char *p = *pp;
    while (*p)
    {
        while (*p == ' ' || *p == ',' || *p == '\t')
        {
            p++;
        }
        if (!*p || *p == ')' || *p == ';')
        {
            break;
        }

        const char *start = p;
        while (*p && *p != ',' && *p != ')' && *p != ';' && *p != ' ' && *p != '\t')
        {
            p++;
        }

        int len = (int)(p - start);
        if (len > 0 && len < 64)
        {
            char name[64];
            strncpy(name, start, len);
            name[len] = '\0';
            add_signal(mod, name, is_input, is_output);
        }
    }
    *pp = p;
}

/* Parse a single-line always block:
     always @(posedge CLK) Q <= D;          (DFF)
     always @(*) if (EN) Q = D;             (DLATCH)
   Multi-line always blocks are not supported (documented restriction). */
static void parse_always(const char *line, vmodule_t *mod)
{
    const char *p = strstr(line, "always");
    if (!p)
    {
        return;
    }
    p += 6;
    while (*p && *p != '(')
    {
        p++;
    }
    if (*p != '(')
    {
        return;
    }
    p++;

    int is_latch = 0;
    char clk[64] = "";
    if (strncmp(p, "posedge", 7) == 0)
    {
        p += 7;
        while (*p == ' ')
        {
            p++;
        }
        const char *cs = p;
        while (*p && *p != ')' && *p != ' ' && *p != ';')
        {
            p++;
        }
        int cl = (int)(p - cs);
        if (cl < 64)
        {
            strncpy(clk, cs, cl);
            clk[cl] = '\0';
        }
    }
    else
    {
        is_latch = 1;
    }

    const char *eq = strstr(line, "<=");
    if (!eq)
    {
        eq = strchr(line, '=');
    }
    if (!eq)
    {
        return;
    }

    /* destination (before <=) */
    const char *qend = eq;
    while (qend > line && qend[-1] == ' ')
    {
        qend--;
    }
    const char *qstart = qend;
    while (qstart > line && qstart[-1] != ' ' && qstart[-1] != ')' && qstart[-1] != '(')
    {
        qstart--;
    }
    char q[64] = "";
    int ql = (int)(qend - qstart);
    if (ql > 0 && ql < 64)
    {
        strncpy(q, qstart, ql);
        q[ql] = '\0';
    }
    else
    {
        return;
    }

    /* source (after <= / =) */
    const char *ds = eq + 2;
    while (*ds == ' ')
    {
        ds++;
    }
    const char *de = ds;
    while (*de && *de != ';' && *de != ' ' && *de != ')')
    {
        de++;
    }
    char d[64] = "";
    int dl = (int)(de - ds);
    if (dl > 0 && dl < 64)
    {
        strncpy(d, ds, dl);
        d[dl] = '\0';
    }
    else
    {
        return;
    }

    /* latch enable from "if (EN)" */
    if (is_latch)
    {
        const char *ifp = strstr(line, "if");
        if (ifp)
        {
            const char *op = strchr(ifp, '(');
            if (op)
            {
                op++;
                while (*op == ' ')
                {
                    op++;
                }
                const char *oe = op;
                while (*oe && *oe != ')' && *oe != ' ' && *oe != ';')
                {
                    oe++;
                }
                int ol = (int)(oe - op);
                if (ol < 64)
                {
                    strncpy(clk, op, ol);
                    clk[ol] = '\0';
                }
            }
        }
    }
    if (!clk[0])
    {
        return;
    }

    if (mod->num_ffs < MAX_FF)
    {
        vff_t *f = &mod->ffs[mod->num_ffs++];
        strncpy(f->name, q, 63);
        strncpy(f->d, d, 63);
        strncpy(f->clk, clk, 63);
        strncpy(f->type, is_latch ? "DLATCH" : "DFF", 15);
        add_signal(mod, q, 0, 0);
        add_signal(mod, d, 0, 0);
        add_signal(mod, clk, 0, 0);
    }
}

/* Parse a single assign statement: assign dst = expr; */
static void parse_assign(const char *line, vmodule_t *mod)
{
    /* Skip "assign" keyword */
    const char *p = line + 6;

    /* Parse destination */
    while (*p == ' ')
    {
        p++;
    }
    const char *dst_start = p;
    while (*p && *p != ' ' && *p != '=')
    {
        p++;
    }
    int dst_len = (int)(p - dst_start);
    if (dst_len <= 0 || dst_len >= 64)
    {
        return;
    }

    char dst[64];
    strncpy(dst, dst_start, dst_len);
    dst[dst_len] = '\0';

    /* Skip = */
    while (*p && *p != '=')
    {
        p++;
    }
    if (*p != '=')
    {
        return;
    }
    p++; /* skip = */
    while (*p == ' ')
    {
        p++;
    }

    /* Parse expression: src1 [op src2] */
    char src1[64], src2[64], op[16];
    src1[0] = src2[0] = op[0] = '\0';

    const char *s1 = p;
    while (*p && *p != ' ' && *p != ';' && *p != '&' && *p != '|' && *p != '^')
    {
        p++;
    }
    int s1_len = (int)(p - s1);
    if (s1_len < 64)
    {
        strncpy(src1, s1, s1_len);
        src1[s1_len] = '\0';
    }

    /* Skip spaces to find operator */
    while (*p == ' ')
    {
        p++;
    }

    if (*p == '&' || *p == '|' || *p == '^' || *p == '~')
    {
        /* Check for ~ prefix (NAND, NOR, XNOR) */
        if (*p == '~')
        {
            strcpy(op, "~");
            p++;
        }
        char o = *p;
        if (o == '&')
        {
            strcpy(op, "AND");
        }
        else if (o == '|')
        {
            strcpy(op, "OR");
        }
        else if (o == '^')
        {
            strcpy(op, "XOR");
        }
        p++;

        while (*p == ' ')
        {
            p++;
        }
        const char *s2 = p;
        while (*p && *p != ';' && *p != ' ')
        {
            p++;
        }
        int s2_len = (int)(p - s2);
        if (s2_len < 64)
        {
            strncpy(src2, s2, s2_len);
            src2[s2_len] = '\0';
        }
    }
    else if (strncmp(src1, "~", 1) == 0)
    {
        /* NOT: ~src1 */
        strcpy(op, "NOT");
        memmove(src1, src1 + 1, strlen(src1));
    }
    else if (strncmp(src1, "!", 1) == 0)
    {
        strcpy(op, "NOT");
        memmove(src1, src1 + 1, strlen(src1));
    }
    else if (src2[0] == '\0')
    {
        /* Single word — might be a direct connection (buffer) or NOT */
        if (strstr(line, "~") || strstr(line, "!"))
        {
            strcpy(op, "NOT");
            const char *tilde = strchr(line, '~');
            if (!tilde)
            {
                tilde = strchr(line, '!');
            }
            if (tilde)
            {
                tilde++;
                while (*tilde == ' ')
                {
                    tilde++;
                }
                const char *ts = tilde;
                while (*tilde && *tilde != ';' && *tilde != ' ')
                {
                    tilde++;
                }
                int tl = (int)(tilde - ts);
                if (tl < 64)
                {
                    strncpy(src1, ts, tl);
                    src1[tl] = '\0';
                }
            }
        }
    }

    /* Add signals and assign */
    if (dst[0])
    {
        add_signal(mod, dst, 0, 0);
    }
    if (src1[0])
    {
        add_signal(mod, src1, 0, 0);
    }
    if (src2[0])
    {
        add_signal(mod, src2, 0, 0);
    }

    if (mod->num_assigns < MAX_ASGN)
    {
        vasgn_t *a = &mod->assigns[mod->num_assigns++];
        strncpy(a->dst, dst, 63);
        strncpy(a->op, op, 15);
        strncpy(a->src1, src1, 63);
        strncpy(a->src2, src2, 63);
    }
}

int vparse(const char *filename, vmodule_t *mod)
{
    memset(mod, 0, sizeof(*mod));

    FILE *f = fopen(filename, "r");
    if (!f)
    {
        perror(filename);
        return -1;
    }

    char buf[4096];
    int in_module = 0;

    while (fgets(buf, sizeof(buf), f))
    {
        char *line = trim(buf);
        if (!*line || line[0] == '/' || line[0] == '#')
        {
            continue;
        }
        if (line[0] == '*')
        {
            continue;
        }

        /* module name (...) ; */
        if (strncmp(line, "module", 6) == 0)
        {
            in_module = 1;
            const char *p = line + 6;
            while (*p == ' ')
            {
                p++;
            }
            const char *ns = p;
            while (*p && *p != '(' && *p != ' ' && *p != '\t')
            {
                p++;
            }
            int nl = (int)(p - ns);
            if (nl < 64)
            {
                strncpy(mod->name, ns, nl);
                mod->name[nl] = '\0';
            }

            /* Parse port list */
            while (*p && *p != '(')
            {
                p++;
            }
            if (*p == '(')
            {
                p++;
                const char *pend = p;
                int depth = 1;
                while (*pend)
                {
                    if (*pend == '(')
                    {
                        depth++;
                    }
                    if (*pend == ')')
                    {
                        depth--;
                        if (depth == 0)
                        {
                            break;
                        }
                    }
                    pend++;
                }
                /* Extract port names between parens */
                char ports[4096];
                int plen = (int)(pend - p);
                if (plen > 4095)
                {
                    plen = 4095;
                }
                strncpy(ports, p, plen);
                ports[plen] = '\0';
                /* Split by commas, track direction from keywords */
                int cur_in = 1, cur_out = 0; /* default: inputs */
                char *token = strtok(ports, " ,");
                while (token)
                {
                    if (strcmp(token, "input") == 0)
                    {
                        cur_in = 1;
                        cur_out = 0;
                    }
                    else if (strcmp(token, "output") == 0)
                    {
                        cur_in = 0;
                        cur_out = 1;
                    }
                    else
                    {
                        add_signal(mod, token, cur_in, cur_out);
                    }
                    token = strtok(NULL, " ,");
                }
            }
            continue;
        }

        if (!in_module)
        {
            continue;
        }
        if (strncmp(line, "endmodule", 9) == 0)
        {
            in_module = 0;
            continue;
        }

        /* input/output declarations override the default from port list */
        if (strncmp(line, "input", 5) == 0)
        {
            const char *p = line + 5;
            parse_id_list(&p, mod, 1, 0);
            continue;
        }
        if (strncmp(line, "output", 6) == 0)
        {
            const char *p = line + 6;
            parse_id_list(&p, mod, 0, 1);
            continue;
        }

        /* assign statement */
        if (strncmp(line, "assign", 6) == 0)
        {
            parse_assign(line, mod);
            continue;
        }

        /* always block (single-line) */
        if (strncmp(line, "always", 6) == 0)
        {
            parse_always(line, mod);
            continue;
        }
    }

    fclose(f);
    return 0;
}

/* ---- Netlist builder ---- */

static int find_gate_type(const char *op)
{
    if (strcasecmp(op, "AND") == 0)
    {
        return NODE_AND;
    }
    if (strcasecmp(op, "OR") == 0)
    {
        return NODE_OR;
    }
    if (strcasecmp(op, "NOT") == 0)
    {
        return NODE_NOT;
    }
    if (strcasecmp(op, "NAND") == 0)
    {
        return NODE_NAND;
    }
    if (strcasecmp(op, "NOR") == 0)
    {
        return NODE_NOR;
    }
    if (strcasecmp(op, "XOR") == 0)
    {
        return NODE_XOR;
    }
    if (strcasecmp(op, "XNOR") == 0)
    {
        return NODE_XNOR;
    }
    if (strcasecmp(op, "DFF") == 0)
    {
        return NODE_DFF;
    }
    if (strcasecmp(op, "JKFF") == 0)
    {
        return NODE_JKFF;
    }
    if (strcasecmp(op, "DLATCH") == 0)
    {
        return NODE_DLATCH;
    }
    return -1;
}

netlist_t *vbuild(const vmodule_t *mod)
{
    netlist_t *nl = netlist_create();

    /* First pass: create all signal nodes (INPUT/OUTPUT) */
    for (int i = 0; i < mod->num_signals; i++)
    {
        const vsignal_t *s = &mod->signals[i];
        node_type_t t = s->is_output ? NODE_OUTPUT : NODE_INPUT;
        node_t *n = netlist_add_node(nl, s->name, t, 0, i * 2);
        n->is_public = (s->is_input || s->is_output) ? 1 : 0;
    }

    /* Second pass: create gate nodes and wires from assignments */
    for (int ai = 0; ai < mod->num_assigns; ai++)
    {
        const vasgn_t *a = &mod->assigns[ai];
        int gtype = find_gate_type(a->op);
        if (gtype < 0)
        {
            continue;
        }

        /* Create a unique gate name */
        char gname[64];
        snprintf(gname, sizeof(gname), "%s_%d", a->op, ai);

        node_t *gate = netlist_add_node(nl, gname, gtype, 0, 0);

        node_t *src1 = netlist_find_node(nl, a->src1);
        node_t *dst = netlist_find_node(nl, a->dst);

        if (src1 && gate)
        {
            netlist_add_wire(nl, src1, 0, gate, 0);
        }

        if (a->src2[0])
        {
            node_t *src2 = netlist_find_node(nl, a->src2);
            if (src2 && gate)
            {
                netlist_add_wire(nl, src2, 0, gate, 1);
            }
        }

        if (gate && dst)
        {
            netlist_add_wire(nl, gate, 0, dst, 0);
        }
    }

    /* Third pass: sequential elements from always blocks */
    for (int fi = 0; fi < mod->num_ffs; fi++)
    {
        const vff_t *f = &mod->ffs[fi];
        int gtype = (strcmp(f->type, "DLATCH") == 0) ? NODE_DLATCH : NODE_DFF;
        char gname[64];
        snprintf(gname, sizeof(gname), "%s_%d", f->type, fi);
        node_t *ff = netlist_add_node(nl, gname, gtype, 0, 0);
        node_t *d = netlist_find_node(nl, f->d);
        node_t *clk = netlist_find_node(nl, f->clk);
        node_t *q = netlist_find_node(nl, f->name);
        if (d && ff)
        {
            netlist_add_wire(nl, d, 0, ff, 0);
        }
        if (clk && ff)
        {
            netlist_add_wire(nl, clk, 0, ff, 1);
        }
        if (ff && q)
        {
            netlist_add_wire(nl, ff, 0, q, 0);
        }
    }

    return nl;
}
