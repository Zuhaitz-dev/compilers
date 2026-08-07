# b-compiler-x64

A compiler for Ken Thompson's 1969 B programming language, targeting native x86-64 Linux.

## Features
- Quite complete B language support (as per the Honeywell 6000 manual: https://www.thinkage.ca/gcos/expl/b/manu/manu.html).
- Native x86-64 code generation (via NASM).
- Simple C-based runtime library (`libb`).
- Recursive descent parser with AST generation.

> It is more like a super-set of B, but not the whole B as specified in this specific manual.

## Building the Compiler
To build the compiler (`bc`) and the runtime library (`libb.o`), simply run:
```bash
make
```

## Compiling B Programs
You can use the provided Makefile to compile B source files into executables. For example, to compile `examples/hello.b`:
```bash
make examples/hello
./examples/hello
```

## Examples
The `examples/` directory contains several B programs:
- `hello.b`: A classic "Hello, World!" example.
- `fib.b`: Generates the Fibonacci sequence.
- `fact.b`: Calculates factorials from 1 to 10.
- `fizzbuzz.b`: Classic FizzBuzz using a `for` loop and `%`.
- `prime.b`: Sieve of Eratosthenes with a global vector.
- `gcd.b`: Greatest common divisor (Euclidean algorithm).
- `collatz.b`: Prints the Collatz (3n+1) sequence for 27.
- `sort.b`: Bubble sort of a 10-element vector.
- `towers.b`: Towers of Hanoi via recursion.
- `argv.b`: Prints the command-line arguments via `main(argc, argv)`.
- `game.b`: A "B-Pong" game showcase using Raylib.

## Implementation Details
The compiler is written in C and generates NASM-compatible assembly. It uses a 64-bit word size (8 bytes), so packed chars (`'abcd'`) can contain 8 chars (or 10 6-bit BCD chars). This defers from the manual for simple hardware reasons.

Strings in B use a `*` escape character (like `*n` for newline) instead of the modern `\`.

## License
MIT

## Contributing
Feel free to do whatever you want with this codebase, I don't really care.
