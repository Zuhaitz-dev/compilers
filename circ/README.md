# circ — ASCII Circuit Compiler

Read a `.circ` file with ASCII art digital logic and compile it to
simulation, truth tables, C, or Verilog.

```
make && make test
```

## Circuit format (v2)

```
A>----+---[AND]----Q<     # AND gate: A> input port, Q< output port
      |
B>----+

Cin>---[NOT]---Cout<      # NOT gate, multi-letter port names

D>---[DFF]---Q<           # D flip-flop (rising edge triggered)
     |
CLK>-+

A>----+---[XOR]---T1      # Wire labels (T1) connect signals across rows
      |
B>----+
T1----+---[XOR]---S<
Cin>-/
```

### Ports

| Syntax | Meaning |
|--------|---------|
| `NAME>` | Public input port |
| `NAME<` | Public output port |
| `NAME`  | Internal wire label (driven once, consumed ≥1) |

The same signal name may appear in several places; every occurrence must
use the same marker. A name used once as `A>` and once plain is an error.

### Gates

| Gate | Syntax | Inputs |
|------|--------|--------|
| AND  | `[AND]` | 2 |
| OR   | `[OR]`  | 2 |
| NOT  | `[NOT]` | 1 |
| NAND | `[NAND]`| 2 |
| NOR  | `[NOR]` | 2 |
| XOR  | `[XOR]` | 2 |
| XNOR | `[XNOR]`| 2 |
| DFF  | `[DFF]` | 2 (D, CLK) |
| JKFF | `[JKFF]`| 3 (J, K, CLK) |
| DLATCH| `[DLATCH]`| 2 (D, EN) |
| GND  | `[GND]` | 0 (constant 0) |
| VCC  | `[VCC]` | 0 (constant 1) |

### Wires

Only three wire characters are supported (diagonals are not):

| Char | Meaning |
|------|---------|
| `-`  | Horizontal wire |
| `\|` | Vertical wire |
| `+`  | Junction (all directions connected) |

Junctions merge nets: any number of labels can feed one gate input pin,
and symmetric gates (AND/OR/XOR/NAND/NOR/XNOR) fold them. Because `+`
connects all four directions, keep the other two arms clear, or the
parser will report a short.

### Pin sockets (geometry)

A gate `[NAME]` at column `gx`, row `gy` has:
- input 0 at `(gx-1, gy)` (left of the bracket)
- input `i` (≥1) at `(gx+i-1, gy+i)` (below the bracket)
- output at `(gx+width, gy)` (right of the bracket)

Sequential gates require **exactly one net per pin** — a junction feeding
both D and CLK is rejected as a short. Combinational gates accept any
number of merged inputs.

### Subcircuits

```
[subcircuit(sub_port=ext_port ...)]
```

Example:
```
[full_adder(A=A0 B=B0 Cin=Cin S=S0 Cout=C0)]
```

Every public input port of a subcircuit must be connected, either by a
wire label on the instance's input net or by a direct wire from another
gate. Each instance gets its own copy of the subcircuit, so flip-flop
state is isolated between instances.

### Comments

Lines starting with `#` are ignored.

### Buses

Append `[N:M]` to a signal name to make it a bus of width `|N-M|+1`.
Gates on buses operate bitwise; the width is inferred from the nets and
must match on every connection.

```
A[3:0]>----+--[XOR]--S[3:0]<      # 4-bit XOR
B[3:0]>----+

D[3:0]>---[DFF]---Q[3:0]<         # 4-bit register (CLK is scalar)
           |
      CLK>+

S>---[NOT]---NS              # 4-bit mux with a 1-bit select:
A[3:0]>----+--[AND]--T1       #   Q = (A&S) | (B&~S)
S>---------+
B[3:0]>----+--[AND]--T2
NS---------+
T1-----+--[OR]--Q[3:0]<
T2-----+
```

Rules:

- `[3:0]` and `[0:3]` both mean "bits 0..3", value bit 0 = bit 0.
- A gate's width equals its data-net width; mixing widths is an error.
- A **width-1 signal feeding a bus gate's data pin is broadcast**
  (replicated), so a 1-bit select can steer a bus, as in the mux above.
  A bus feeding a scalar gate or scalar output is still an error.
- Sequential gates: D/J/K and Q are data (matching width); CLK is always
  scalar. A bus CLK is an error.
- Internal labels adopt the width of the gate that drives them (plain
  `NS` driven by a 4-bit gate is 4-bit).
- Subcircuit instances do not support buses yet (a bus on a subcircuit
  port is an error).
- Buses are up to 16 bits wide; a wider bus is an error.

`--sim` accepts whole-bus values (`A=12`), `--truth` enumerates every
input bit combination (up to 16 bits), and the C backend passes buses as
plain `int` values.

See `examples/alu_reg.circ` for a combined demo: a 4-bit ALU slice
(AND/XOR selected by a 1-bit `OP>`) latched into a 4-bit register —
it exercises buses, scalar broadcast, width inference through internal
labels, and sequential capture in one drawing.

### Sizes

Signal names are limited to 63 characters (a longer name is an error).
There are no other practical size limits: signal counts, subcircuit port
mappings, and chained subcircuit wires grow dynamically.

## Validation

Every circuit is validated on parse; a malformed circuit is a hard error
with a message and grid coordinates instead of silently producing a
wrong netlist:

- undriven gate input, or a sequential pin with >1 signal (short)
- public input driven (short), public output with no driver,
  public input not connected
- internal label with no driver, multiple drivers, or no consumers
- duplicate/conflicting signal names, name collisions with gates
- subcircuit input port not connected
- combinational loops (sequential elements break loops)
- stray diagonal characters (`/`, `\`)
- signal names longer than 63 characters

Warnings (not errors) are printed to stderr for stray wire characters
that belong to no net. `circ --check <file>` validates a file and exits
nonzero on error.

## Usage

```
circ [options] --sim|--truth|--c|--v|--check <file.circ> [inputs...]
```

### Backends

| Flag | Output |
|------|--------|
| `--sim` | Simulate with `NAME=VALUE` inputs |
| `--truth` | Truth table (all input combos; rejected for sequential circuits) |
| `--c` | C code |
| `--v` | Verilog module |
| `--vcd` | VCD waveform (for GTKWave) |
| `--check` | Validate the circuit and exit |
| `--from-v <file.v>` | Convert Verilog to `.circ` |

### Options

| Flag | Purpose |
|------|---------|
| `-I<dir>` | Add subcircuit search directory |
| `-o FILE` (or `--output=FILE`) | Write output to FILE instead of stdout |
| `--cycles=N` | Simulate N clock cycles (auto-toggles CLK) |
| `--clock=NAME` | Specify clock port name (default: CLK) |

### Examples

```bash
# Simulate a half-adder
./circ --sim examples/half_adder.circ A=1 B=0

# 4-bit counter over 16 cycles
./circ --sim examples/counter4.circ --cycles=16

# Truth table
./circ --truth examples/full_adder.circ

# Generate C code (call circuit() once per clock cycle)
./circ --c examples/adder2.circ -o /tmp/adder2.c
gcc -c /tmp/adder2.c

# Generate Verilog
./circ --v examples/chain.circ -o /tmp/chain.v
iverilog -o /tmp/chain.out /tmp/chain.v
```

## Generated C contract

`circuit()` is a synchronous, edge-detecting function: call it once per
clock cycle with the clock input toggled between calls. Flip-flop state
persists across calls (per-instance `static` state). The function
relaxes to a fixed point internally, so ripple counters and latches are
supported. Ripple-clocked flip-flops may evaluate differently in iverilog
than in `--sim` because iverilog sees the x→1 transition of a derived
clock at startup.

Two forms:

```bash
# 1. Library — embed and call circuit() yourself
circ --c examples/alu_reg.circ -o /tmp/alu.c
gcc -c /tmp/alu.c            # compile-only; the file is a function, not a program

# 2. Standalone runnable program (main() mirrors --sim)
circ --c --driver examples/alu_reg.circ -o /tmp/alu.c
gcc /tmp/alu.c -o /tmp/alu
/tmp/alu A=12 B=10 OP=1 --cycles=2     # Q=6, identical to --sim
```

The driver's `main()` accepts `NAME=VALUE` inputs (masked to the port
width), `--cycles=N`, and auto-toggles the input named `CLK`. To use a
clock with another name, set it explicitly (`NAME=VALUE`).

## Backend restrictions (documented)

- `--truth` is undefined for sequential circuits and is rejected.
- `--from-v` supports a structural subset: `assign` gates, single-line
  `always @(posedge CLK) Q <= D;` (DFF) and `always @(*) if (EN) Q = D;`
  (DLATCH). Multi-line `always` blocks and module instantiation are not
  parsed.

## Project structure

```
circ/
├── src/
│   ├── main.c          — CLI entry
│   ├── netlist.h/c     — Data structures (nodes, wires, netlist)
│   ├── grid.h/c        — 2D grid (text file loader)
│   ├── parser.h/c      — ASCII → netlist pipeline + validation
│   ├── subcircuit.h/c  — Subcircuit resolution, per-instance clones
│   ├── backend.h       — Backend interface
│   ├── backend_sim.c   — Simulation + truth table
│   ├── backend_c.c     — C code generation
│   ├── backend_v.c     — Verilog generation
│   ├── backend_vcd.c   — VCD waveform generation
│   ├── vparser.c/h     — Verilog → netlist
│   ├── vlayout.c/h     — Netlist → ASCII layout
│   ├── error.h         — Error reporting
│   ├── arena.h         — Arena allocator
│   └── circ_internal.h — Shared compat helpers
├── examples/           — Example .circ files
├── tests/
│   ├── negative/       — Intentionally broken circuits (each must fail)
│   ├── stress/         — Large circuits (300+ signals / chained subs / 20-port subs)
│   ├── cc_semantic.py  — Compile+run generated C, compare to --sim
│   └── v_semantic.py   — iverilog+vvp, compare to --sim
├── .clang-format       — Code style
├── .clang-tidy         — Static analysis
└── Makefile            — Build system
```

## Build

```bash
make              # release build
make DEBUG=1      # debug build
make SANITIZE=1   # address + UB sanitizers
make format       # clang-format
```

## Fuzzing

`make fuzz` runs a random fuzzer (`tests/fuzz.py`) that generates
circuits (structured DAGs over scalar/bus signals including DFF, JKFF,
DLATCH, GND/VCC and clocked-by-CLK blocks, plus random mutations of the
examples and saved regressions) and checks that every parse either
rejects cleanly or evaluates identically across `--sim`, the compiled
`--c --driver` program, and iverilog. `make fuzz_asan` runs it against
the sanitizer build; `make campaign` runs `tests/run_campaign.py`, which
spreads many seeds across the machine's cores. Failures are saved to
`tests/fuzz/regressions/`.

Notes on the comparison harness:

- Edge-triggered flops (DFF/JKFF) capture via the Verilog delta model:
  flops on the same clock edge snapshot their D before any of them
  updates, while flops clocked by another flop's output (ripple counters)
  capture sequentially as their clock rises. `--sim` and the generated C
  both follow this model.
- The sim and C driver auto-toggle only an input literally named `CLK`;
  any other clock input is held at its vector value (constant-clock
  coverage), and the iverilog testbench mirrors that.
- The iverilog cross-check is skipped when a circuit has a latch or a
  flop clocked by an internal signal, because iverilog's power-on
  X→1 transitions on ripple wires fire spurious posedges at time 0.

The campaign has covered 96,000+ circuits across 24 seeds with zero
failures, and has previously caught and fixed real bugs: bus-JKFF
per-bit logic, and simultaneous (non-blocking) flop capture in `--sim`
and the C backend.

