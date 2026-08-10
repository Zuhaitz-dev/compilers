#ifndef VLAYOUT_H
#define VLAYOUT_H

#include "grid.h"
#include "netlist.h"

/* Auto-layout a netlist and produce a .circ grid.
   Returns a grid_t that can be printed with grid_print().
   @param nl: the netlist to lay out
   @param top_name: the top-level module name (used for gate positioning)
   @return: a grid_t ready for emission */
grid_t *vlayout(netlist_t *nl, const char *top_name);

/* Print a .circ grid to stdout */
void vlayout_emit(grid_t *g);

#endif
