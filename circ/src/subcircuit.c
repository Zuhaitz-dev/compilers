#include "subcircuit.h"
#include "circ_internal.h"
#include "grid.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SEARCH_DIRS 32
#define MAX_CACHE 64
#define MAX_SUB_DEPTH 32

static char *search_dirs[MAX_SEARCH_DIRS];
static int num_search_dirs = 0;

typedef struct
{
    char name[64];
    netlist_t *netlist; /* template; clones are handed out per instance */
} cache_entry_t;

static cache_entry_t cache[MAX_CACHE];
static int cache_count = 0;

void subcircuit_add_search_dir(const char *dir)
{
    if (num_search_dirs >= MAX_SEARCH_DIRS)
    {
        return;
    }
    search_dirs[num_search_dirs] = circ_strdup(dir);
    num_search_dirs++;
}

/* Locate the .circ file for a subcircuit name. Returns 0 and fills path. */
static int subcircuit_find(const char *name, const char *parent_dir, char *path, int path_size)
{
    FILE *f = NULL;
    snprintf(path, path_size, "%s/%s.circ", parent_dir, name);
    f = fopen(path, "r");
    if (!f)
    {
        snprintf(path, path_size, "%s.circ", name);
        f = fopen(path, "r");
    }
    if (!f)
    {
        snprintf(path, path_size, "examples/%s.circ", name);
        f = fopen(path, "r");
    }
    if (!f)
    {
        for (int i = 0; i < num_search_dirs; i++)
        {
            snprintf(path, path_size, "%s/%s.circ", search_dirs[i], name);
            f = fopen(path, "r");
            if (f)
            {
                break;
            }
        }
    }
    if (!f)
    {
        return -1;
    }
    fclose(f);
    return 0;
}

/* Template lookup or parse (cached). Never returns a shared mutable copy
   to a caller; use subcircuit_load() which deep-copies per instance. */
static netlist_t *subcircuit_template(const char *name, const char *parent_dir)
{
    for (int i = 0; i < cache_count; i++)
    {
        if (strcmp(cache[i].name, name) == 0)
        {
            return cache[i].netlist;
        }
    }

    char path[512];
    if (subcircuit_find(name, parent_dir, path, sizeof(path)) != 0)
    {
        return NULL;
    }

    grid_t *g = grid_load(path);
    if (!g)
    {
        return NULL;
    }
    netlist_t *nl = parse_circuit(g);
    grid_free(g);
    if (!nl)
    {
        return NULL;
    }

    if (cache_count < MAX_CACHE)
    {
        strncpy(cache[cache_count].name, name, 63);
        cache[cache_count].netlist = nl;
        cache_count++;
    }
    return nl;
}

static int load_depth = 0;

netlist_t *subcircuit_load(const char *name, const char *parent_dir)
{
    if (load_depth >= MAX_SUB_DEPTH)
    {
        return NULL; /* runaway / recursive subcircuits */
    }
    load_depth++;
    netlist_t *tpl = subcircuit_template(name, parent_dir);
    load_depth--;
    if (!tpl)
    {
        return NULL;
    }
    /* Every instance gets its own copy so sequential state is isolated. */
    return netlist_clone(tpl);
}
