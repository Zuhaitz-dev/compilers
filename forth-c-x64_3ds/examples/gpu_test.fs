\ -----------------------------------------------------------
\ 3DS C23 Forth - Bare-Metal GPU Graphics Benchmark
\ -----------------------------------------------------------

CR ." Initializing 3DS Top Screen GPU Canvas..." CR
GR-CLS GR-FLUSH

." Drawing solid color palette boxes..." CR
\ Draw Red Box
255 0 0 GR-COLOR!
20 20 100 60 GR-RECT!

\ Draw Green Box
0 255 0 GR-COLOR!
140 20 100 60 GR-RECT!

\ Draw Blue Box
0 150 255 GR-COLOR!
260 20 100 60 GR-RECT!

GR-FLUSH

." Running interactive Touch-to-Draw Canvas!" CR
." Touch and drag on bottom screen to paint live on top screen!" CR
." Hold [A] button while touching to clear canvas and exit." CR

: PAINT-LOOP
    255 220 0 GR-COLOR! \ Paint in gold!
    BEGIN
        WAIT-TOUCH
        \ Scale bottom 320x240 touch X to top 400x240 display X
        SWAP 5 * 4 / SWAP
        \ Draw an 8x8 brush block at touch coordinates
        8 8 GR-RECT!
        GR-FLUSH
        KEY-A?
    UNTIL
    GR-CLS GR-FLUSH
    ." Canvas cleared. Exiting graphics demo!" CR
;

PAINT-LOOP
