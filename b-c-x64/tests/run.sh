#!/bin/sh
# Test script for b-c-x64.
cd "$(dirname "$0")/.."

check() {
    name=$1
    shift
    expected=$1
    shift
    if ! make "examples/$name" > /dev/null 2>&1; then
        echo "FAIL: could not compile examples/$name.b"
        exit 1
    fi
    out=$(./examples/$name "$@")
    if [ "$out" != "$expected" ]; then
        echo "FAIL: $name.b output:"
        printf '%s\n' "$out"
        echo "expected:"
        printf '%s\n' "$expected"
        exit 1
    fi
    echo "PASS: $name.b"
}

make clean > /dev/null
if ! make > /dev/null 2>&1; then
    echo "FAIL: build failed"
    exit 1
fi

check hello "Hello from B!"
check fib "0 1 1 2 3 5 8 13 21 34 55 "
check fizzbuzz "$(cat <<'EOF'
1
2
Fizz
4
Buzz
Fizz
7
8
Fizz
Buzz
11
Fizz
13
14
FizzBuzz
16
17
Fizz
19
Buzz
Fizz
22
23
Fizz
Buzz
26
Fizz
28
29
FizzBuzz
EOF
)"
check prime "2 3 5 7 11 13 17 19 23 29 31 37 41 43 47 53 59 61 67 71 73 79 83 89 97 "
check gcd "$(cat <<'EOF'
12
1
21
13
EOF
)"
check collatz "$(cat <<'EOF'
27 82 41 124 62 31 94 47 142 71 214 107 322 161 484 242 121 364 182 91 274 137 412 206 103 310 155 466 233 700 350 175 526 263 790 395 1186 593 1780 890 445 1336 668 334 167 502 251 754 377 1132 566 283 850 425 1276 638 319 958 479 1438 719 2158 1079 3238 1619 4858 2429 7288 3644 1822 911 2734 1367 4102 2051 6154 3077 9232 4616 2308 1154 577 1732 866 433 1300 650 325 976 488 244 122 61 184 92 46 23 70 35 106 53 160 80 40 20 10 5 16 8 4 2 1
steps: 111
EOF
)"
check sort "2 5 9 12 17 21 33 42 60 77 "
check towers "$(cat <<'EOF'
Towers of Hanoi, 3 disks:
Move disk 1 from A to C
Move disk 2 from A to B
Move disk 1 from C to B
Move disk 3 from A to C
Move disk 1 from B to A
Move disk 2 from B to C
Move disk 1 from A to C
EOF
)"
check argv "hello world " hello world

echo "PASS: all b-c-x64 tests passed"
