#ifndef BACKEND_H
#define BACKEND_H

#include "netlist.h"

void eval_node(netlist_t *nl, node_t *n);
void eval_settle(netlist_t *nl);
void backend_sim(netlist_t *nl, int argc, char **argv);
int backend_truth(netlist_t *nl);
void backend_c(netlist_t *nl);
void backend_c_driver(netlist_t *nl);
void backend_v(netlist_t *nl);
void backend_vcd(netlist_t *nl, int argc, char **argv);

#endif
