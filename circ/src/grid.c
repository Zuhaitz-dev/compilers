#include "grid.h"
#include "circ_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

grid_t *grid_load(const char *filename)
{
    FILE *f = fopen(filename, "r");
    if (!f)
    {
        perror(filename);
        return NULL;
    }

    grid_t *g = calloc(1, sizeof(grid_t));
    g->height = 0;
    g->width = 0;
    g->data = NULL;

    char line[4096];
    while (fgets(line, sizeof(line), f))
    {
        int len = (int)strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        {
            line[--len] = '\0';
        }
        if (len > g->width)
        {
            g->width = len;
        }
        char **gd = realloc(g->data, (size_t)(g->height + 1) * sizeof(char *));
        if (!gd)
        {
            fclose(f);
            grid_free(g);
            return NULL;
        }
        g->data = gd;
        g->data[g->height] = circ_strdup(line);
        g->height++;
    }
    fclose(f);

    for (int i = 0; i < g->height; i++)
    {
        int len = (int)strlen(g->data[i]);
        if (len < g->width)
        {
            char *gd = realloc(g->data[i], g->width + 1);
            if (!gd)
            {
                grid_free(g);
                return NULL;
            }
            g->data[i] = gd;
            memset(g->data[i] + len, ' ', g->width - len);
            g->data[i][g->width] = '\0';
        }
    }
    return g;
}

char grid_get(const grid_t *g, int x, int y)
{
    if (!g || !g->data || y < 0 || y >= g->height || x < 0 || x >= g->width)
    {
        return ' ';
    }
    return g->data[y][x];
}

int grid_in_bounds(const grid_t *g, int x, int y)
{
    return g && x >= 0 && x < g->width && y >= 0 && y < g->height;
}

void grid_free(grid_t *g)
{
    if (!g)
    {
        return;
    }
    for (int i = 0; i < g->height; i++)
    {
        free(g->data[i]);
    }
    free(g->data);
    free(g);
}
