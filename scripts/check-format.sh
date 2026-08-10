#!/bin/sh
# Verify all tracked C/C++ sources in the compiler projects conform to the
# shared root .clang-format. Override the binary with CLANG_FORMAT if needed.
cd "$(dirname "$0")/.."

CLANG_FORMAT=${CLANG_FORMAT:-clang-format}

files=$(git ls-files -- '*.c' '*.h' '*.cpp' '*.hpp' '*.cc' '*.hh' |
    grep -E '^(b-c-x64|forth-c-x64-3ds|circ|pinnacle-c-pinnacle|pascal-cpp-x64)/' || true)

if [ -z "$files" ]; then
    echo "No C/C++ files found."
    exit 1
fi

fail=0
for f in $files; do
    if ! "$CLANG_FORMAT" --dry-run --Werror "$f"; then
        echo "Formatting issue: $f"
        fail=1
    fi
done

if [ "$fail" -ne 0 ]; then
    echo "FAIL: format check failed (run 'clang-format -i' on the files above)"
    exit 1
fi

echo "PASS: format check"
