#pragma once

#include "gfx.h"
#include "sprite_sheet.h"
#include "board.h"
#include <string>
#include <vector>

// ====================== DECORATIVE VIRUS ======================

struct DecorVirus {
    float x, y;
    int color;
    float phase;
    float size;
};

std::vector<DecorVirus> generate_decor_viruses(int count, int sw, int sh);

// ====================== LAYOUT ======================

struct LayoutMetrics {
    int screen_w, screen_h;
    int cell_size;
    int board_w, board_h;
    int board_y; // vertical top of boards

    // Left player board
    int left_board_x;
    // Right bot board
    int right_board_x;
    // Left side panel (player info)
    int left_panel_x;
    int panel_w;
    // Right side panel (bot info)
    int right_panel_x;
    // Center HUD (between the two boards)
    int center_x, center_w;
};

LayoutMetrics compute_layout(int screen_w, int screen_h);

// ====================== UI RENDERER ======================
// All high-level drawing. Uses only the Gfx interface — no raylib/Tiny3D calls.
// To port: implement Gfx for your platform, pass it here.

class UIRenderer {
  public:
    explicit UIRenderer(Gfx &gfx);

    // Sprite sheet (optional — enables sprite-based rendering)
    void set_sprite_sheet(const SpriteSheet *sprites);

    // Background
    void draw_bg(Gfx::Tex bg_tex, int sw, int sh);

    // Title screen
    void draw_title_screen(int selected,
                           const std::vector<DecorVirus> &viruses, float time,
                           int sw, int sh);

    // Game screen components
    void draw_board(const PlayerBoard &board, int bx, int by, int cell_size,
                    float time);
    void draw_side_panel(const char *label, const PlayerBoard &board, int px,
                         int py, int panel_w, int cell_size, float time);
    void draw_center_hud(int wins, int losses, int player_viruses,
                         int bot_viruses, int player_attacks, int bot_attacks,
                         int round_num, const LayoutMetrics &layout);
    void draw_game_screen(const PlayerBoard &player, const PlayerBoard &bot,
                          int player_attacks, int bot_attacks, int wins,
                          int losses, int round_num,
                          const LayoutMetrics &layout, float time);

    // Round end overlay
    void draw_round_end(bool player_won, const LayoutMetrics &layout);

    // Bot battle
    void draw_bot_battle_hud(const std::string &bot1_name,
                             const std::string &bot2_name, int wins1,
                             int wins2, int trial, int total_trials,
                             const LayoutMetrics &layout);
    void draw_bot_battle_result(const std::string &bot1_name,
                                const std::string &bot2_name, int wins1,
                                int wins2, int sw, int sh);

  private:
    Gfx &gfx_;
    const SpriteSheet *sprites_;

    // Internal helpers
    void draw_virus_sprite(int cx, int cy, float radius, int color, float time,
                           float phase);
    void draw_pill_cell(int x, int y, int w, int h, int color, bool top,
                        bool bottom, bool left, bool right);
    void draw_single_cap_sprite(int dst_x, int dst_y, int dst_w, int dst_h,
                                 int color);
    bool draw_hcap_sprite(int dst_x, int dst_y, int dst_w, int dst_h,
                          int h1, int h2);
    bool draw_vcap_sprite(int dst_x, int dst_y, int dst_w, int dst_h,
                          int top_color, int bottom_color);
    void draw_active_capsule(const Capsule &cap, int bx, int by, int cs);
    void draw_stamped_piece(int r, int c, int color, int bx, int by, int cs);
    void draw_connected_hcap(int r, int c_left, int c_right,
                             int color_left, int color_right,
                             int bx, int by, int cs);
    void draw_connected_vcap(int r_top, int r_bottom, int c,
                             int top_color, int bottom_color,
                             int bx, int by, int cs);
};
