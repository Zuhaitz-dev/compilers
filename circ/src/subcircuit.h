#ifndef SUBCIRCUIT_H
#define SUBCIRCUIT_H

#include "netlist.h"

void subcircuit_add_search_dir(const char *dir);
netlist_t *subcircuit_load(const char *name, const char *parent_dir);

#endif
