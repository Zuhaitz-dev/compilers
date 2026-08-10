# forth-c-x64-3ds

A C23 Forth compiler/interpreter that runs on Linux x86-64 and the Nintendo
3DS (via libctru). One codebase with a small PAL for the platform differences.

## Build (Linux)

```bash
make
make test
```

## Build (3DS)

Requires devkitPro with `DEVKITARM` set:

```bash
make -f Makefile.3ds
make -f Makefile.3ds run
```

Produces `forth_3ds.3dsx`.

## Features

- Direct-threaded code with a 32-byte cache-aligned word header
- REPL and file loading
- 3DS extras: touch, buttons, GPU canvas (`GR-*` words)

## Examples

- `examples/test.fs`: hardware checks
- `examples/gpu_test.fs`: GPU canvas demo
- `examples/pong.fs`: Pong game

## License

MIT
