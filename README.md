# compilers

A collection of small compilers and toolchains, one per directory, each with
its own build system and tests. A shared `.clang-format`, `.clang-tidy`,
`scripts/check-format.sh`, and a GitHub Actions workflow keep them consistent.

## Projects

| Directory | What it is | Build | Test |
|-----------|------------|-------|------|
| `b-c-x64` | B language compiler to x86-64 via NASM | `make` | `./tests/run.sh` |
| `forth-nasm-x64` | Forth interpreter/compiler in x86-64 NASM | `make` | `./tests/run.sh` |
| `forth-c-x64-3ds` | C23 Forth for Linux x86-64 and the Nintendo 3DS | `make` | `make test` |
| `circ` | ASCII circuit drawings to simulation, C, or Verilog | `make` | `make test` |
| `pinnacle-c-pinnacle` | Assembler, simulator, and disassembler for a custom ISA | `make` | `make smoke` |
| `pascal-cpp-x64` | Pascal compiler to native code via LLVM | `cmake` | `./tests/run.sh` |

Each project has its own README.

## Top-level targets

```bash
make test          # run every project's test suite
make format        # clang-format all C/C++ sources in place
make lint          # clang-tidy (currently circ only)
make check_format  # verify sources conform to the shared .clang-format
make clean         # clean every project
```

## CI

`.github/workflows/ci.yml` runs on push/PR to `main`. It checks formatting
and builds/tests every project on ubuntu-24.04.

## License

MIT. See `LICENSE`.
