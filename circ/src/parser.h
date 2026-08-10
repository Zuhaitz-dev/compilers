#ifndef PARSER_H
#define PARSER_H

#include "error.h"
#include "grid.h"
#include "netlist.h"

/* Parse a circuit file. Sets error on failure, returns NULL. */
netlist_t *parse_circuit(grid_t *g);
netlist_t *parse_circuit_with_dir(grid_t *g, const char *dir);

/* Error-aware versions (recommended) */
netlist_t *parse_circuit_er(grid_t *g, const char *dir, circ_error_t *err);

/* Debug: dump grid to stderr */
void parse_debug_grid(grid_t *g);

#endif
