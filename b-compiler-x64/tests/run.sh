#!/bin/sh
# Test script for b-compiler-x64.
cd "$(dirname "$0")/.."

make clean > /dev/null
if ! make > /dev/null 2>&1; then
    echo "FAIL: build failed"
    exit 1
fi

# hello.b
if ! make examples/hello > /dev/null 2>&1; then
    echo "FAIL: could not compile examples/hello.b"
    exit 1
fi
out=$(./examples/hello)
if [ "$out" != "Hello from B!" ]; then
    echo "FAIL: hello.b output: '$out'"
    exit 1
fi

# fib.b
if ! make examples/fib > /dev/null 2>&1; then
    echo "FAIL: could not compile examples/fib.b"
    exit 1
fi
out=$(./examples/fib)
expected="0 1 1 2 3 5 8 13 21 34 55 "
if [ "$out" != "$expected" ]; then
    echo "FAIL: fib.b output: '$out'"
    exit 1
fi

echo "PASS: all b-compiler tests passed"
