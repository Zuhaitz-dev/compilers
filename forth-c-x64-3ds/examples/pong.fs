\ -----------------------------------------------------------
\ 3DS C23 Forth - Bare-Metal Pong Game
\ -----------------------------------------------------------

CR ." Loading Forth Pong..." CR

\ Game Variables
VARIABLE PUSH-X
VARIABLE PUSH-Y
VARIABLE BALL-X
VARIABLE BALL-Y
VARIABLE BALL-VX
VARIABLE BALL-VY
VARIABLE PADDLE-X

\ Initialize Game State
: INIT-GAME
    200 BALL-X !
    120 BALL-Y !
      3 PUSH-X !
      2 PUSH-Y !
    160 PADDLE-X !
    GR-CLS GR-FLUSH
;

\ Draw Game Objects (White Ball, Cyan Paddle)
: DRAW-SCENE
    \ Clear screen
    GR-CLS
    
    \ Draw Ball (8x8 white square)
    255 255 255 GR-COLOR!
    BALL-X @ BALL-Y @ 8 8 GR-RECT!
    
    \ Draw Paddle (50x8 cyan rectangle at bottom Y=220)
    0 200 255 GR-COLOR!
    PADDLE-X @ 220 50 8 GR-RECT!
    
    GR-FLUSH
;

\ Update Ball Physics & Collision
: UPDATE-BALL
    \ Move ball
    BALL-X @ PUSH-X @ + BALL-X !
    BALL-Y @ PUSH-Y @ + BALL-Y !
    
    \ Bounce off left/right walls (0 to 400)
    BALL-X @ 10 < IF -1 PUSH-X @ * PUSH-X ! THEN
    BALL-X @ 390 > IF -1 PUSH-X @ * PUSH-X ! THEN
    
    \ Bounce off top wall (Y=0)
    BALL-Y @ 10 < IF -1 PUSH-Y @ * PUSH-Y ! THEN
    
    \ Paddle Collision Check (Y >= 212 and X within paddle span)
    BALL-Y @ 212 > IF
        BALL-X @ PADDLE-X @ >= 
        BALL-X @ PADDLE-X @ 50 + <= AND IF
            -1 PUSH-Y @ * PUSH-Y !
        THEN
    THEN
;

\ Main Game Loop
: PONG-GAME
    INIT-GAME
    BEGIN
        \ Check touch input to slide paddle left/right
        TOUCH? IF TOUCH-COORDS DROP 5 * 4 / PADDLE-X ! THEN
        
        UPDATE-BALL
        DRAW-SCENE
        
        \ Exit loop if [A] button is held
        KEY-A?
    UNTIL
    GR-CLS GR-FLUSH
    ." Exiting Pong. Good game!" CR
;

." Type PONG-GAME to start playing!" CR
