# compilers

A collection of small compilers and language/toolchain experiments, each in
its own self-contained directory with its own build system and tests. A shared
`.clang-format`, `.clang-tidy`, `scripts/check-format.sh`, and a GitHub Actions
workflow keep them all consistent and continuously tested.

## Projects

| Directory | What it is | Build | Test |
|-----------|------------|-------|------|
| `b-compiler-x64` | Compiler for Ken Thompson's 1969 B language, targeting native x86-64 via NASM | `make` | `./tests/run.sh` |
| `forth-nasm-x64` | A Forth interpreter/compiler written entirely in x86-64 NASM | `make` | `./tests/run.sh` |
| `forth-c-x64_3ds` | C23 direct-threaded Forth for Linux and Nintendo 3DS (libctru) | `make` | `make test` |
| `circ` | ASCII-circuit compiler: `.circ` drawings to simulation, C, or Verilog | `make` | `make test` |
| `Pinnacle` | Assembler + simulator + disassembler for a custom ISA | `make` | `make smoke` |
| `pascal-cpp-x64` | Pascal compiler in C++23 (work in progress) | — | — |

Each project has its own `README.md` with details.

## Top-level targets

```bash
make test          # run every project's test suite
make format        # clang-format all C/C++ sources in place
make lint          # clang-tidy (currently circ only)
make check_format  # verify sources conform to the shared .clang-format
make clean         # clean every project
```

## Continuous integration

`.github/workflows/ci.yml` runs on push/PR to `main` and:
- checks formatting via `scripts/check-format.sh` (clang-format 22.1.8),
- builds and tests `b-compiler-x64`, `forth-nasm-x64`, `forth-c-x64_3ds`
  (Linux target), `circ` (build + clang-tidy lint + full test suite), and
  `Pinnacle` (build + smoke test).

## License

MIT — see `LICENSE`.
