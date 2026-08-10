# pascal-cpp-x64

A Pascal compiler written in C++20 that lowers a Pascal subset to native code
via LLVM (object files). It is built around a classic pipeline: lexer →
parser → AST → semantic analysis → LLVM IR → machine code.

## Supported language

**Types:** `integer`, `real`, `boolean`, `char`, and 1-D arrays
(`array[lo..hi] of <scalar>`). String literals are supported in `writeln`
and `const`.

**Declarations:** `program`, `const` (literal constants), `var` (multiple
names per line, arrays), `procedure name(params)`, and
`function name(params) : type`. Parameters are `[var] name[, name] : type`
(value or reference).

**Statements:** assignment (`x := expr`, `a[i] := expr`), `begin...end`,
`if...then...else`, `while...do`, `for i := a to|downto b do`,
`repeat...until`, `case ... of ... else ... end`, `writeln(e1, e2, ...)`,
and procedure/function calls.

**Expressions:** integer/real/boolean/char/string literals, variables,
array indexing, function calls, unary `-` and `not`, binary
`+ - * / div mod and or`, and comparisons `= <> < <= > >=` with Pascal
precedence. `div`/`mod` are integer; `/` is real division; `and`/`or`/`not`
work on booleans and (bitwise) on integers. Functions set their result via
`fname := expr` and may recurse.

Semantic analysis catches undeclared identifiers, duplicate declarations,
type mismatches (with `integer` → `real` widening), wrong call arity,
non-boolean conditions, invalid array indices, and assignments to
constants/subprograms.

## Building

Requires CMake and a recent LLVM (tested with LLVM 22).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Usage

```bash
./build/pascalc <file.pas> [--ir] [-o output.o]
```

- `file.pas` — Pascal source.
- `--ir` — print the generated LLVM IR to stdout.
- `-o FILE` — object file output (default `output.o`).

To turn the object file into a runnable program (the generated module
defines `main`):

```bash
gcc output.o -o program
./program
```

Example:

```bash
./build/pascalc examples/hello.pas -o hello.o
gcc hello.o -o hello
./hello
```

## Tests

```bash
./tests/run.sh
```

Builds the compiler, compiles/links/runs every program in
`tests/programs/`, and diffs stdout against the golden files in
`tests/expected/`. Invalid programs in `tests/negative/` must be rejected.

## Project layout

```
include/pascal/Lexer/  — tokens + lexer
include/pascal/AST/    — AST node definitions + TypeRef
include/pascal/Parser/ — recursive descent parser
include/pascal/Sema/   — scoped symbol table + type checker
include/pascal/CodeGen/— LLVM IR + object-file emission
src/                   — matching implementations
tests/                 — programs, golden outputs, negative cases
```

## Limitations / future work

- No records, pointers, sets, or nested subprograms.
- Arrays are 1-D with constant bounds; no multidimensional arrays.
- No `readln`, no separate compilation, no string type variables.
- Local subprogram declarations are not supported.
