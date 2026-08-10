#ifndef NETLIST_H
#define NETLIST_H

/* Size limits */
#define NAME_MAX 64
#define LABEL_MAX 128
#define LINE_MAX 512

/* Parser constants */
#define INIT_BINDING_CAP 4
#define TRACE_ID_MULTIPLIER 100
#define QUEUE_CELL_MULTIPLIER 2

/* Connection directions */
#define CONN_R (1 << 0)
#define CONN_L (1 << 1)
#define CONN_D (1 << 2)
#define CONN_U (1 << 3)

/* Cardinal directions */
#define NUM_DIRS 4

typedef enum
{
    NODE_INPUT,
    NODE_OUTPUT,
    NODE_AND,
    NODE_OR,
    NODE_NOT,
    NODE_NAND,
    NODE_NOR,
    NODE_XOR,
    NODE_XNOR,
    NODE_DFF,
    NODE_JKFF,
    NODE_DLATCH,
    NODE_CLOCK,
    NODE_GND,
    NODE_VCC,
    NODE_SUBCIRCUIT,
} node_type_t;

extern const char *node_type_names[];

typedef struct wire
{
    struct node *src;
    int src_pin;
    struct node *dst;
    int dst_pin;
} wire_t;

typedef struct
{
    char name[NAME_MAX];
    struct node *external_node;
} port_binding_t;

typedef struct
{
    char sub_port_name[NAME_MAX];
    char external_name[NAME_MAX];
} port_mapping_t;

typedef struct netlist netlist_t;

typedef struct node
{
    char name[NAME_MAX];
    node_type_t type;
    int is_public; /* 1 = explicit port (A> input, Q< output), 0 = internal label */
    int width;     /* 1 = scalar, >1 = bus */
    int bus_msb;   /* bus bit index range (scalar: 0) */
    int bus_lsb;
    int num_inputs_needed;
    wire_t **inputs;
    int num_inputs;
    int input_cap;
    wire_t **outputs;
    int num_outputs;
    int output_cap;
    int id;
    int value;
    int x, y;
    netlist_t *sub_netlist;
    port_binding_t *input_bindings;
    int num_input_bindings;
    int input_bindings_cap;
    port_binding_t *output_bindings;
    int num_output_bindings;
    int output_bindings_cap;
    port_mapping_t *port_mappings;
    int num_port_mappings;
    int port_mappings_cap;
    int prev_clock;
    int state;
    int pending;       /* next-state snapshot for simultaneous edge capture */
    int pending_valid; /* 1 if pending holds an unapplied capture */
} node_t;

struct netlist
{
    node_t **nodes;
    int num_nodes;
    int node_cap;
};

netlist_t *netlist_create(void);
node_t *netlist_add_node(netlist_t *nl, const char *name, node_type_t type, int x, int y);
int netlist_add_wire(netlist_t *nl, node_t *src, int src_pin, node_t *dst, int dst_pin);
netlist_t *netlist_clone(netlist_t *src);
int netlist_get_inputs_count(netlist_t *nl);
int netlist_get_outputs_count(netlist_t *nl);
node_t **netlist_get_inputs(netlist_t *nl, int *count);
node_t **netlist_get_outputs(netlist_t *nl, int *count);
int netlist_has_sequential(netlist_t *nl);
node_t *netlist_find_node(netlist_t *nl, const char *name);
node_t *netlist_find_port(netlist_t *nl, const char *name);
void netlist_print(netlist_t *nl);
void netlist_free(netlist_t *nl);

int cell_connections(char c, int gate_cell);

#endif
