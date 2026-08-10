\ Forth test suite for forth-c-x64-3ds
\ Each test line must print exactly one -1 (pass) or 0 (fail)

\ Clear the initial 30 left by the demo eval
DROP

\ --- Stack operations ---
1 2 + 3 = .
2 1 - 1 = .
3 4 * 12 = .
1 DUP + 2 = .
1 2 DROP 1 = .
1 2 NIP 2 = .
1 2 3 ROT 1 = . DROP DROP
\ --- Comparisons ---
1 2 < -1 = .
2 1 > -1 = .
1 1 = -1 = .
0 0= -1 = .
3 5 AND 1 = .
3 5 OR 7 = .
3 5 XOR 6 = .
0 INVERT -1 = .
\ --- Math extensions ---
5 3 MOD 2 = .
10 3 /MOD SWAP 1 = . 3 = .
5 1+ 6 = .
\ --- Signed comparisons ---
-3 -2 < -1 = .
-2 -3 > -1 = .
\ --- >= / <= ---
5 3 >= -1 = .
3 5 <= -1 = .
5 5 >= -1 = .
5 5 <= -1 = .
\ --- DEPTH ---
DEPTH 0 = .
1 DEPTH 1 = . DROP
\ --- STATE ---
STATE 0 = .
\ --- ABS / NEGATE ---
-5 ABS 5 = .
5 NEGATE -5 = .
0 ABS 0 = .
\ --- MIN / MAX ---
3 7 MIN 3 = .
3 7 MAX 7 = .
-3 -7 MIN -7 = .
-3 -7 MAX -3 = .
\ --- TICK / EXECUTE ---
' DUP 1 SWAP EXECUTE 1 = . DROP
' DROP 42 SWAP EXECUTE 0= .
\ --- Conditionals ---
: test-if IF 42 THEN ;
1 test-if 42 = .
: test-if-else IF 10 ELSE 20 THEN ;
1 test-if-else 10 = .
0 test-if-else 20 = .
\ --- BEGIN/UNTIL ---
: test-loop 0 BEGIN DUP 1+ DUP 5 = UNTIL ;
test-loop 5 = .
\ --- DO/LOOP ---
: test-do 0 5 0 DO I + LOOP ;
test-do 10 = .
\ --- LEAVE ---
: test-leave 0 10 0 DO I + I 3 = IF LEAVE THEN LOOP ;
test-leave 6 = .
\ --- Variables ---
VARIABLE vx 42 vx ! vx @ 42 = .
VARIABLE vy vy @ 0 = .
\ --- Colon definitions ---
: square DUP * ;
4 square 16 = .
: five 5 ;
five 5 = .
\ --- Memory ops ---
HERE 256 ALLOT HERE SWAP - 256 = .
\ --- Comments & strings ---
( this is a comment )
\ --- Nested conditionals ---
: nested IF IF 99 THEN THEN ;
1 1 nested 99 = .
