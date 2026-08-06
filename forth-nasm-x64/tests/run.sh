#!/bin/sh
# Test script for forth-nasm-x64.
cd "$(dirname "$0")/.."

make clean > /dev/null
if ! make > /dev/null 2>&1; then
    echo "FAIL: build failed"
    exit 1
fi

output=$(./forth tests/test.fs < /dev/null 2>&1)

# '. ' prints "value " and CR prints a newline, so each printed value is a
# line containing only the number and trailing whitespace.
actual=$(printf '%s\n' "$output" | sed -n 's/^\(-\{0,1\}[0-9][0-9]*\)[[:space:]]*$/\1/p')

expected="3
7
200
4
2
0
-1
-1
-1
10
1
3
3
42
16
6
99
20"

if [ "$actual" != "$expected" ]; then
    echo "FAIL: expected:"
    printf '%s\n' "$expected"
    echo "got:"
    printf '%s\n' "$actual"
    echo "--- raw output ---"
    printf '%s\n' "$output"
    exit 1
fi

echo "PASS: all forth-nasm tests passed"
