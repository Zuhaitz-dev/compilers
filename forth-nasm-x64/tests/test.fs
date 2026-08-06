\ Forth test suite for forth-nasm-x64.
\ Each line prints an expected value via '.' (value + trailing space).
\ Only uses words known to work in the current implementation.

\ --- Arithmetic ---
1 2 + . CR
3 4 + . CR
10 20 * . CR
20 5 / . CR
20 3 MOD . CR

\ --- Comparisons (false = 0, true = -1) ---
1 2 = . CR
1 1 = . CR
2 3 < . CR
3 2 > . CR

\ --- Stack operations ---
5 DUP + . CR
1 2 DROP . CR
1 2 SWAP + . CR
1 2 OVER + . CR

\ --- Variables ---
VARIABLE vx
42 vx ! vx @ . CR

\ --- Colon definitions ---
: square DUP * ;
4 square . CR
: add3 + + ;
1 2 3 add3 . CR

\ --- IF/ELSE/THEN ---
: test-if IF 99 THEN ;
1 test-if . CR
: test-else IF 10 ELSE 20 THEN ;
0 test-else . CR
