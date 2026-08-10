# circ

An ASCII circuit compiler. Reads `.circ` files of ASCII art and compiles
them to simulation, truth tables, C, or Verilog.

```bash
make && make test
```

## Format

```
A>----+---[AND]----Q<     # AND gate: A> input, Q< output
      |
B>----+
```

- `NAME>` public input port, `NAME<` public output port, `NAME` internal wire.
- Gates: `[AND] [OR] [NOT] [NAND] [NOR] [XOR] [XNOR] [DFF] [JKFF] [DLATCH] [GND] [VCC]`.
- Wires: `-` horizontal, `|` vertical, `+` junction (all four directions).
- Subcircuits: `[name(port=wire ...)]`.
- Comments: lines starting with `#`.
- Buses: `NAME[N:M]`; width is inferred and must match on every connection.
  A width-1 signal feeding a bus gate's data pin is broadcast.
- Signal names are limited to 63 characters.

Circuits are validated on parse; invalid circuits are errors with grid
coordinates. `circ --check <file>` validates and exits nonzero on error.

## Usage

```
circ [options] --sim|--truth|--c|--v|--check <file.circ> [inputs...]
```

| Flag | Output |
|------|--------|
| `--sim` | Simulate with `NAME=VALUE` inputs |
| `--truth` | Truth table (rejected for sequential circuits) |
| `--c` | C code |
| `--v` | Verilog module |
| `--vcd` | VCD waveform |
| `--check` | Validate and exit |
| `--from-v <file.v>` | Convert Verilog to `.circ` |

Options: `-I<dir>` subcircuit search path, `-o FILE` output file,
`--cycles=N` clock cycles, `--clock=NAME` clock port (default `CLK`).

Examples:

```bash
./circ --sim examples/half_adder.circ A=1 B=0
./circ --truth examples/full_adder.circ
./circ --c examples/chain.circ -o /tmp/chain.c
./circ --v examples/chain.circ -o /tmp/chain.v
```

## Generated C

`circuit()` is synchronous and edge-detecting: call it once per clock cycle
with the clock toggled between calls. Flip-flop state persists across calls.
`circ --c --driver <file>` adds a `main()` that mirrors `--sim`.

## Tests

`make test` runs the full suite: golden outputs, negative cases, stress
tests, C/Verilog semantic cross-checks, and a fuzz smoke test.

## Notes

- `--truth` is undefined for sequential circuits.
- `--from-v` supports a structural subset (assign gates, simple always blocks).

## License

MIT
