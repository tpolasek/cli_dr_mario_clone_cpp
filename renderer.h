#pragma once

#include "board.h"
#include "bot/bot_registry.h"

// ====================== ANSI HELPERS ======================

inline const char* clr_ansi(int c) {
    switch (c) {
        case RED:    return "\033[91m";
        case YELLOW: return "\033[93m";
        case BLUE:   return "\033[94m";
        default:     return "\033[0m";
    }
}

inline const char* dark_ansi(int c) {
    switch (c) {
        case RED:    return "\033[31m";
        case YELLOW: return "\033[33m";
        case BLUE:   return "\033[34m";
        default:     return "\033[0m";
    }
}

// ====================== RENDERING ======================

// Render a single board at the given screen position.
void render_board(const PlayerBoard& board, const char* label, int x_offset,
                  bool show_controls, int attack_count, int anim_frame,
                  int round_num = 0, int wins = 0, int losses = 0);

// Render the full game view (both boards + status line).
void render_game(const PlayerBoard& player, const PlayerBoard& bot,
                 int player_attacks, int bot_attacks, int& anim_frame,
                 int wins = 0, int losses = 0, int round_num = 0);

// Render the round-end screen (win or loss). Waits for caller to handle input.
void render_round_end(bool player_won, int wins, int losses, int round_num, int total_rounds);

// ====================== MENU RENDERING ======================

void render_enter_alt_screen();
void render_clear_screen();
void render_title();
void render_virus_menu();
void render_speed_menu();
void render_mode_menu();
void render_bot_menu(const std::vector<BotEntry>& bots);
