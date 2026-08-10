#!/bin/sh
FORTH_BIN="${1:-./forth_linux}"
TEST="${2:-tests/test.fs}"

if [ ! -f "$FORTH_BIN" ]; then
    echo "FAIL: $FORTH_BIN not found (build with 'make')"
    exit 1
fi

output=$(SILENT=1 "$FORTH_BIN" 2>&1 < "$TEST")

# Strip ANSI, keep only the first token of each line.
# Test results from '.' are the first token on their line
# (e.g. "-1  ok" -> first token "-1").
results=$(echo "$output" \
    | sed 's/\x1b\[[0-9;]*[a-zA-Z]//g' \
    | sed 's/^[[:space:]]*//' \
    | grep -E '^(-1|0)[[:space:]]' \
    | sed 's/[[:space:]].*//')

failed=0
total=0
for r in $results; do
    total=$((total + 1))
    if [ "$r" != "-1" ]; then
        failed=$((failed + 1))
        echo "FAIL: test #$total (got $r)"
    fi
done

if [ "$total" -eq 0 ]; then
    echo "FAIL: no test results detected"
    echo "--- stripped output ---"
    echo "$output" | sed 's/\x1b\[[0-9;]*[a-zA-Z]//g'
    exit 1
fi

if [ "$failed" -eq 0 ]; then
    echo "PASS: all $total tests passed"
else
    echo "FAIL: $failed/$total tests failed"
    echo "--- stripped output (first 30 lines) ---"
    echo "$output" | sed 's/\x1b\[[0-9;]*[a-zA-Z]//g' | head -30
    exit 1
fi
