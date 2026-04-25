#include "raylib.h"

#define RENDER_W 1920
#define RENDER_H 1080
#define WIN_W    (RENDER_W / 2)
#define WIN_H    (RENDER_H / 2)

int main(void)
{
    InitWindow(WIN_W, WIN_H, "raylib example");

    Texture2D bg = LoadTexture("bg.png");
    RenderTexture2D target = LoadRenderTexture(RENDER_W, RENDER_H);

    while (!WindowShouldClose()) {
        BeginTextureMode(target);
            ClearBackground(RAYWHITE);
            DrawTexture(bg, 0, 0, WHITE);
            DrawText("Hello, raylib!", 810, 530, 30, BLACK);
        EndTextureMode();

        BeginDrawing();
            DrawTexturePro(target.texture,
                (Rectangle){ 0, 0, RENDER_W, -RENDER_H },
                (Rectangle){ 0, 0, WIN_W, WIN_H },
                (Vector2){ 0, 0 }, 0, WHITE);
        EndDrawing();
    }

    UnloadRenderTexture(target);
    UnloadTexture(bg);
    CloseWindow();
    return 0;
}
