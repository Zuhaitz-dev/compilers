\ -----------------------------------------------------------
\ C23 Direct-Threaded Forth - SD Card Hardware Verification
\ -----------------------------------------------------------

CR
." => TESTING VARIABLES & MEMORY" CR
VARIABLE SCORE
1000 SCORE !
SCORE @ . CR

." => TESTING CONDITIONAL BRANCHING (IF/ELSE/THEN)" CR
: CHECK-VAL ( n -- )
    DUP 50 > IF
        DROP 999
    ELSE
        DROP 111
    THEN . CR
;
80 CHECK-VAL
20 CHECK-VAL

." => TESTING LOOP CONTROL FLOW (BEGIN/UNTIL)" CR
: COUNTDOWN ( n -- )
    BEGIN
        DUP . 
        1-
        DUP 0=
    UNTIL
    DROP CR
;
5 COUNTDOWN

." => TESTING HARDWARE TOUCH LOOP" CR
: TOUCH-TEST
    BEGIN
        WAIT-TOUCH
        + . CR
        KEY-A?
    UNTIL
;
." Tap screen to sum coords! Hold [A] while tapping to exit loop." CR
TOUCH-TEST
