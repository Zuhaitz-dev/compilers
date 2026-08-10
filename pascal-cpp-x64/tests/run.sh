#!/bin/sh
# Test script for pascal-cpp-x64.
# Builds pascalc, compiles each tests/programs/*.pas to an object file, links
# it, runs it, and compares stdout against tests/expected/<name>.txt.
cd "$(dirname "$0")/.."

JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)

# Auto-detect the LLVM CMake directory if LLVM_DIR is not already set.
if [ -z "$LLVM_DIR" ]; then
    for lc in llvm-config llvm-config-22; do
        if command -v "$lc" > /dev/null 2>&1; then
            LLVM_DIR=$("$lc" --cmakedir)
            break
        fi
    done
fi

echo "=== Building pascalc ==="
if ! cmake -S . -B build -DCMAKE_BUILD_TYPE=Release ${LLVM_DIR:+-DLLVM_DIR="$LLVM_DIR"} > /dev/null; then
    echo "FAIL: cmake configure failed"
    exit 1
fi
if ! cmake --build build -j"$JOBS" > /dev/null; then
    echo "FAIL: build failed"
    exit 1
fi

PASCALC=./build/pascalc
OUTDIR=build/tests
mkdir -p "$OUTDIR"

failed=0
count=0
for src in tests/programs/*.pas; do
    base=$(basename "$src" .pas)
    count=$((count + 1))
    exp="tests/expected/$base.txt"
    if [ ! -f "$exp" ]; then
        echo "FAIL: $base (no expected output file)"
        failed=$((failed + 1))
        continue
    fi

    if ! "$PASCALC" "$src" -o "$OUTDIR/$base.o" 2> "$OUTDIR/$base.err"; then
        echo "FAIL: $base (compile)"
        sed 's/^/    /' "$OUTDIR/$base.err"
        failed=$((failed + 1))
        continue
    fi
    if ! gcc "$OUTDIR/$base.o" -o "$OUTDIR/$base" 2> "$OUTDIR/$base.link.err"; then
        echo "FAIL: $base (link)"
        sed 's/^/    /' "$OUTDIR/$base.link.err"
        failed=$((failed + 1))
        continue
    fi
    "$OUTDIR/$base" > "$OUTDIR/$base.out" 2>&1
    if cmp -s "$OUTDIR/$base.out" "$exp"; then
        echo "PASS: $base"
    else
        echo "FAIL: $base"
        diff "$exp" "$OUTDIR/$base.out" | head -10
        failed=$((failed + 1))
    fi
done

# Negative tests: each invalid program must be rejected by the compiler.
neg_failed=0
neg_count=0
for src in tests/negative/*.pas; do
    [ -e "$src" ] || continue
    base=$(basename "$src" .pas)
    neg_count=$((neg_count + 1))
    if "$PASCALC" "$src" -o "$OUTDIR/neg_$base.o" > "$OUTDIR/neg_$base.out" 2>&1; then
        echo "FAIL: $base (should have been rejected)"
        neg_failed=$((neg_failed + 1))
    else
        echo "PASS: $base (rejected)"
    fi
done

echo
if [ "$failed" -eq 0 ] && [ "$neg_failed" -eq 0 ]; then
    echo "PASS: all $count pascal tests + $neg_count negative tests passed"
else
    echo "FAIL: $failed/$count program tests, $neg_failed/$neg_count negative tests failed"
    exit 1
fi
