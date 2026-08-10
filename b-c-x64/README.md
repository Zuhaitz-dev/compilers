# b-c-x64

A compiler for Ken Thompson's 1969 B language, targeting native x86-64 Linux
via NASM. The compiler itself is written in C.

## Build

```bash
make
```

## Compile a B program

```bash
make examples/hello
./examples/hello
```

## Examples

- `hello.b`: Hello, World!
- `fib.b`: Fibonacci sequence
- `fact.b`: Factorials
- `fizzbuzz.b`, `prime.b`, `gcd.b`, `collatz.b`, `sort.b`, `towers.b`, `argv.b`: more demos
- `game.b`: A Pong game using Raylib

## Notes

- B strings use `*` escapes (`*n` for newline), not `\`.
- Words are 8 bytes, so `'abcd'` packs 8 chars.

## License

MIT
