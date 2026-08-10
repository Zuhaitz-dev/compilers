# pinnacle-c-pinnacle

A weekend experiment: a custom 16-bit instruction set (inspired by LC-3)
with a full toolchain, written in C.

- `pasm`: assembler
- `pvm`: simulator
- `pdis`: disassembler

## Build

```bash
make
```

## Use

```bash
./bin/pasm examples/hello.asm
./bin/pvm
```

## Examples

`examples/` contains assembly programs (hello, fibonacci, fizzbuzz, and
more). `benchmark.sh` times a program across several runs.

## License

MIT
