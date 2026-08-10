# forth-nasm-x64

A minimal Forth interpreter/compiler written entirely in x86-64 Linux
assembly (NASM).

## Build

```bash
make
```

## Run

```bash
./forth lib/core.f
```

Loads the standard library, then drops into the REPL. You can also pass a
`.f` file directly.

## Features

- Direct-threaded inner interpreter
- Variables, conditionals, loops
- Interactive REPL and file loading
- Standard library in `lib/core.f`

## Notes

- 64-bit cells.
- The hardware stack (`rsp`) is the return stack; a reserved block is the data stack.

## License

MIT
