#pragma once

#include "raylib.h"

// Undef raylib color macros that conflict with our game color constants
// (our RED=1, YELLOW=2, BLUE=3 from constants.h)
#undef RED
#undef YELLOW
#undef BLUE

#include "board.h"
#include <string>
#include <vector>

// ====================== COLORS ======================

Color pill_color(int c);
Color pill_dark_color(int c);

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
  int left_x, left_y;
  int right_x, right_y;
  int board_w, board_h;
};

LayoutMetrics compute_layout(int screen_w, int screen_h);

// ====================== DRAWING ======================

void draw_bg(Texture2D bg, int sw, int sh);
void draw_virus_sprite(int cx, int cy, float radius, int color, float time,
                       float phase);
void draw_board(const PlayerBoard &board, int bx, int by, int cell_size,
                float time);
void draw_next_piece(const PlayerBoard &board, int bx, int by, int cell_size,
                     const char *label);
void draw_title_screen(int selected, const std::vector<DecorVirus> &viruses,
                       float time, int sw, int sh);
void draw_winners_grid(int wins, int losses, const LayoutMetrics &layout);
void draw_game_hud(const PlayerBoard &player, const PlayerBoard &bot,
                   int player_attacks, int bot_attacks, int wins, int losses,
                   int round_num, const LayoutMetrics &layout, float time);
void draw_round_end_screen(bool player_won, int wins, int losses,
                           int round_num, int sw, int sh);
void draw_bot_battle_hud(const std::string &bot1_name,
                         const std::string &bot2_name, int wins1, int wins2,
                         int trial, int total_trials,
                         const LayoutMetrics &layout);
void draw_bot_battle_result_screen(const std::string &bot1_name,
                                   const std::string &bot2_name, int wins1,
                                   int wins2, int sw, int sh);
