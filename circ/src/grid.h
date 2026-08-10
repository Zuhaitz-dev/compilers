#ifndef GRID_H
#define GRID_H

typedef struct
{
    char **data;
    int width;
    int height;
} grid_t;

grid_t *grid_load(const char *filename);
char grid_get(const grid_t *g, int x, int y);
int grid_in_bounds(const grid_t *g, int x, int y);
void grid_free(grid_t *g);

#endif
