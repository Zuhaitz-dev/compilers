# pascal-cpp-x64

A Pascal compiler in C++20 that lowers a Pascal subset to native code via
LLVM: lexer -> parser -> AST -> semantic analysis -> LLVM IR -> object file.

## Language

- Types: `integer`, `real`, `boolean`, `char`, `array[lo..hi] of <scalar>`.
  String literals in `writeln` and `const`.
- Declarations: `program`, `const`, `var`, `procedure`, `function` with
  `[var]` params (value or reference) and local `var`s.
- Statements: assignment (`x := e`, `a[i] := e`), `begin...end`, `if/else`,
  `while`, `for to/downto`, `repeat...until`, `case`, `writeln(e1, e2, ...)`,
  calls.
- Expressions: literals, variables, indexing, calls, unary `-`/`not`,
  `+ - * / div mod and or`, comparisons, Pascal precedence.
- `div`/`mod` are integer; `/` is real; functions set their result via
  `fname := expr` and may recurse.

Semantic analysis rejects undeclared identifiers, duplicate declarations,
type mismatches, wrong call arity, and invalid conditions.

## Build

Requires CMake and LLVM (tested with LLVM 22).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Use

```bash
./build/pascalc <file.pas> [--ir] [-o out.o]
gcc out.o -o program
./program
```

`--ir` prints the LLVM IR. The generated module defines `main`.

## Tests

```bash
./tests/run.sh
```

Compiles, links, and runs `tests/programs/*.pas`, diffs stdout against
`tests/expected/`, and requires `tests/negative/*.pas` to be rejected.

## Limitations

No records, pointers, sets, multidimensional arrays, `readln`, string type
variables, or nested subprograms.

## License

MIT
