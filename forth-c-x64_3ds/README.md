# forth-c-x64_3ds

A C23 Forth compiler and interpreter targeting both Linux x86-64 and the Nintendo 3DS (ARM11, via libctru).

## Features
- Direct-threaded code (DTC) inner interpreter with a 32-byte cache-aligned word header.
- Same codebase compiles for Linux and 3DS through a small PAL (Platform Abstraction Layer).
- Interactive REPL and file loading.
- 3DS extras: touch screen, buttons, and a bare-metal GPU canvas (`GR-*` words).
- Linux test suite (`make test`).

> It is an ANS-inspired subset, designed to be small enough to fit in the 3DS dictionary while staying useful.

## Building for Linux
To build the Forth engine (`forth_linux`):
```bash
    make
```

To build and run the Linux test suite:
```bash
    make test
```

## Building for 3DS
Requires [devkitPro](https://devkitpro.org/) with `DEVKITARM` set. From a devkitPro environment:
```bash
    make -f Makefile.3ds
```

This produces `forth_3ds.3dsx` (with `forth_3ds.smdh` metadata and `icon.png`). To send it to a 3DS over Wi-Fi:
```bash
    make -f Makefile.3ds run
```

## Examples
The `examples/` directory contains `.fs` programs:
- `test.fs`: Hardware verification (variables, branches, loops, touch input).
- `gpu_test.fs`: GPU canvas benchmark / touch-to-paint demo.
- `pong.fs`: Bare-metal Pong game.

## Implementation Details
The engine is written in C23 and uses a threaded-code interpreter (`execute_threaded`) with a computed-goto dispatch table. Cells are 64-bit on x86-64 and 32-bit on the 3DS ARM11. `WordHeader` is forced to exactly 32 bytes so it fits one cache line on the 3DS.

The Linux build is the primary development target and what the test suite runs against; the 3DS build only differs through `src/pal/3ds_pal.c` and the `#ifdef __3DS__` sections.

## License
MIT

## Contributing
Feel free to do whatever you want with it!
