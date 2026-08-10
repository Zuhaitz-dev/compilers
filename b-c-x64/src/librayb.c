#include <raylib.h>
#include <stdio.h>

// Raylib Wrappers for the B language
// These wrappers take many arguments to test the B compiler's support for > 6 arguments.

void b_InitWindow(long width, long height, long title_addr)
{
    char *title = (char *)(title_addr * 8);
    InitWindow((int)width, (int)height, title);
}

long b_WindowShouldClose(void)
{
    return WindowShouldClose() ? 1 : 0;
}

void b_BeginDrawing(void)
{
    BeginDrawing();
}

void b_EndDrawing(void)
{
    EndDrawing();
}

void b_ClearBackground(long r, long g, long b, long a)
{
    Color color = {(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a};
    ClearBackground(color);
}

void b_DrawText(long text_addr, long x, long y, long size, long r, long g, long b, long a)
{
    char *text = (char *)(text_addr * 8);
    Color color = {(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a};
    DrawText(text, (int)x, (int)y, (int)size, color);
}

void b_DrawInt(long val, long x, long y, long size, long r, long g, long b, long a)
{
    char buf[32];
    sprintf(buf, "%ld", val);
    Color color = {(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a};
    DrawText(buf, (int)x, (int)y, (int)size, color);
}

long b_MeasureText(long text_addr, long size)
{
    char *text = (char *)(text_addr * 8);
    return (long)MeasureText(text, (int)size);
}

void b_CloseWindow(void)
{
    CloseWindow();
}

long b_IsKeyDown(long key)
{
    return IsKeyDown((int)key) ? 1 : 0;
}

void b_DrawCircle(long x, long y, long radius, long r, long g, long b, long a)
{
    Color color = {(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a};
    DrawCircle((int)x, (int)y, (float)radius, color);
}

void b_DrawRectangle(long x, long y, long w, long h, long r, long g, long b, long a)
{
    Color color = {(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a};
    DrawRectangle((int)x, (int)y, (int)w, (int)h, color);
}

void b_DrawRectangleRounded(long x, long y, long w, long h, long round, long r, long g, long b,
                            long a)
{
    Color color = {(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a};
    Rectangle rect = {(float)x, (float)y, (float)w, (float)h};
    DrawRectangleRounded(rect, (float)round / 100.0f, 10, color);
}

void b_DrawFPS(long x, long y)
{
    DrawFPS((int)x, (int)y);
}

long b_GetScreenWidth(void)
{
    return (long)GetScreenWidth();
}

long b_GetScreenHeight(void)
{
    return (long)GetScreenHeight();
}

void b_SetTargetFPS(long fps)
{
    SetTargetFPS((int)fps);
}

long b_GetTime(void)
{
    return (long)(GetTime() * 1000.0); // ms
}

void b_DrawLine(long x1, long y1, long x2, long y2, long r, long g, long b, long a)
{
    Color color = {(unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a};
    DrawLine((int)x1, (int)y1, (int)x2, (int)y2, color);
}
