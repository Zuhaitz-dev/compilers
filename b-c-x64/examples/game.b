SCREEN_WIDTH = 800;
SCREEN_HEIGHT = 450;
BALL_RADIUS = 10;
PADDLE_WIDTH = 120;
PADDLE_HEIGHT = 12;

/* Key codes for Raylib */
KEY_LEFT = 263;
KEY_RIGHT = 262;
KEY_SPACE = 32;

/* Global vectors for ball trail */
trail_x[15];
trail_y[15];
trail_ptr { 0 };

main() {
    extrn b_InitWindow, b_SetTargetFPS, b_WindowShouldClose, b_BeginDrawing, b_EndDrawing;
    extrn b_ClearBackground, b_DrawCircle, b_DrawRectangleRounded, b_DrawText, b_DrawInt, b_CloseWindow;
    extrn b_IsKeyDown, b_DrawFPS, b_DrawLine, b_GetTime, b_MeasureText;

    auto ball_x, ball_y, ball_dx, ball_dy;
    auto paddle_x, score, high_score, state;
    auto i, alpha, size, text_w;

    b_InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "B-Pong: Neo Edition");
    b_SetTargetFPS(60);

    ball_x = SCREEN_WIDTH / 2;
    ball_y = SCREEN_HEIGHT / 2;
    ball_dx = 5;
    ball_dy = 5;
    paddle_x = (SCREEN_WIDTH - PADDLE_WIDTH) / 2;
    score = 0;
    high_score = 0;
    state = 0; /* 0: Ready, 1: Playing, 2: GameOver */

    while (b_WindowShouldClose() == 0) {
        if (state == 0) {
            if (b_IsKeyDown(KEY_SPACE)) state = 1;
        } else if (state == 1) {
            /* Input */
            if (b_IsKeyDown(KEY_LEFT)) paddle_x = paddle_x - 9;
            if (b_IsKeyDown(KEY_RIGHT)) paddle_x = paddle_x + 9;

            /* Boundary check for paddle */
            if (paddle_x < 20) paddle_x = 20;
            if (paddle_x > SCREEN_WIDTH - PADDLE_WIDTH - 20) paddle_x = SCREEN_WIDTH - PADDLE_WIDTH - 20;

            /* Update ball */
            ball_x = ball_x + ball_dx;
            ball_y = ball_y + ball_dy;

            /* Update trail */
            trail_x[trail_ptr] = ball_x;
            trail_y[trail_ptr] = ball_y;
            trail_ptr = (trail_ptr + 1) % 15;

            /* Wall bounce */
            if (ball_x + BALL_RADIUS >= SCREEN_WIDTH - 20 || ball_x - BALL_RADIUS <= 20) ball_dx = -ball_dx;
            if (ball_y - BALL_RADIUS <= 20) ball_dy = -ball_dy;

            /* Paddle bounce */
            if (ball_y + BALL_RADIUS >= SCREEN_HEIGHT - 40) {
                if (ball_x >= paddle_x && ball_x <= paddle_x + PADDLE_WIDTH) {
                    ball_dy = -ball_dy;
                    ball_y = SCREEN_HEIGHT - 40 - BALL_RADIUS;
                    score = score + 1;
                    
                    /* Difficulty curve */
                    if (score % 5 == 0) {
                        if (ball_dx > 0) ball_dx++; else ball_dx--;
                        if (ball_dy > 0) ball_dy++; else ball_dy--;
                    }
                }
            }

            /* Reset if lost */
            if (ball_y > SCREEN_HEIGHT) {
                if (score > high_score) high_score = score;
                state = 2;
            }
        } else if (state == 2) {
            if (b_IsKeyDown(KEY_SPACE)) {
                score = 0;
                ball_x = SCREEN_WIDTH / 2;
                ball_y = SCREEN_HEIGHT / 2;
                ball_dx = 5;
                ball_dy = 5;
                state = 1;
            }
        }

        /* Draw */
        b_BeginDrawing();
        b_ClearBackground(250, 250, 252, 255); /* Neo Light Background */

        /* Draw Main Frame */
        b_DrawRectangleRounded(10, 10, SCREEN_WIDTH - 20, SCREEN_HEIGHT - 20, 15, 230, 230, 235, 255);
        b_DrawRectangleRounded(15, 15, SCREEN_WIDTH - 30, SCREEN_HEIGHT - 30, 10, 255, 255, 255, 255);

        /* Draw Center Net (Subtle) */
        b_DrawLine(20, SCREEN_HEIGHT/2, SCREEN_WIDTH-20, SCREEN_HEIGHT/2, 240, 240, 245, 255);

        if (state == 1) {
            /* Draw Ball Trail */
            i = 0;
            while (i < 15) {
                alpha = (i * 255) / 15;
                size = (i * BALL_RADIUS) / 15;
                b_DrawCircle(trail_x[(trail_ptr + i) % 15], trail_y[(trail_ptr + i) % 15], size, 230, 41, 55, alpha / 2);
                i++;
            }

            /* Draw Ball */
            b_DrawCircle(ball_x, ball_y, BALL_RADIUS, 230, 41, 55, 255);
            
            /* Draw Paddle with shadow-like effect */
            b_DrawRectangleRounded(paddle_x + 2, SCREEN_HEIGHT - 40 + 2, PADDLE_WIDTH, PADDLE_HEIGHT, 50, 200, 200, 200, 100);
            b_DrawRectangleRounded(paddle_x, SCREEN_HEIGHT - 40, PADDLE_WIDTH, PADDLE_HEIGHT, 50, 40, 40, 45, 255);
        }

        /* UI: Modern Score Display (Better Aligned) */
        b_DrawText("SCORE", 60, 50, 15, 160, 160, 170, 255);
        b_DrawInt(score, 60, 70, 40, 40, 40, 45, 255);
        
        /* Right Aligned BEST */
        text_w = b_MeasureText("BEST", 15);
        b_DrawText("BEST", SCREEN_WIDTH - 60 - text_w, 50, 15, 160, 160, 170, 255);
        /* Simple heuristic for number width since we don't have b_MeasureInt */
        b_DrawInt(high_score, SCREEN_WIDTH - 85, 70, 40, 230, 41, 55, 255);

        if (state == 0) {
            text_w = b_MeasureText("B-PONG NEO", 40);
            b_DrawText("B-PONG NEO", SCREEN_WIDTH/2 - text_w/2, SCREEN_HEIGHT/2 - 40, 40, 40, 40, 45, 255);
            
            text_w = b_MeasureText("PRESS SPACE TO START", 20);
            b_DrawText("PRESS SPACE TO START", SCREEN_WIDTH/2 - text_w/2, SCREEN_HEIGHT/2 + 20, 20, 160, 160, 170, 255);
        }

        if (state == 2) {
            b_DrawRectangleRounded(SCREEN_WIDTH/2 - 160, SCREEN_HEIGHT/2 - 60, 320, 120, 20, 255, 255, 255, 230);
            
            text_w = b_MeasureText("GAME OVER", 30);
            b_DrawText("GAME OVER", SCREEN_WIDTH/2 - text_w/2, SCREEN_HEIGHT/2 - 30, 30, 230, 41, 55, 255);
            
            text_w = b_MeasureText("PRESS SPACE TO RESTART", 15);
            b_DrawText("PRESS SPACE TO RESTART", SCREEN_WIDTH/2 - text_w/2, SCREEN_HEIGHT/2 + 15, 15, 40, 40, 45, 255);
        }

        b_DrawFPS(SCREEN_WIDTH - 65, SCREEN_HEIGHT - 35);
        
        b_EndDrawing();
    }

    b_CloseWindow();
}
