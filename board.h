#pragma once

#include "constants.h"

#include <array>
#include <vector>
#include <queue>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <iostream>

// ====================== DATA TYPES ======================

struct Piece {
    int color; bool virus; int capId;
};

struct Capsule {
    int r, c;
    int h1, h2;
    int orient; // 0=right 1=up 2=left 3=down

    int r1() const { return r; }
    int c1() const { return c; }
    int r2() const {
        if (orient & 1) return r - 1;
        return r;
    }
    int c2() const {
        if ((orient & 1) == 0) return c + 1;
        return c;
    }
    void rotate() {
        if ((orient & 1) == 0) std::swap(h1, h2);
        orient = (orient + 1) & 3;
    }
};

enum class Phase { PLAYING, GRAVITY };

// ====================== MOVE ABSTRACTION ======================

enum class Move { NONE, LEFT, RIGHT, DOWN, ROTATE, DROP, QUIT };

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

// ====================== PLAYER STATE ======================

struct PlayerBoard {
    std::array<std::array<Piece, COLS>, ROWS> grid;
    Capsule cap, nxt;
    int score = 0;
    int total_viruses = 0;
    int cleared_viruses = 0;
    int next_cap_id = 1;
    std::vector<int> cascade_colors;
    Phase phase = Phase::PLAYING;
    bool game_over = false;
    bool game_won = false;
    unsigned int rng_state = 1;

    int board_rand();
    int board_rnd_color();
    void board_spawn(Capsule& c);
    void clear_grid();
    bool cell_free(int r, int c) const;
    bool fits(const Capsule& c) const;
    void stamp(const Capsule& c);
    int find_and_remove_matches();
    void flush_cascade(std::queue<int>& opponent_attacks);
    bool is_partner(int r, int c, int dr, int dc, int capId) const;
    void swap_cells(int r1, int c1, int r2, int c2);
    void clear_cell(int r, int c);
    bool gravity_step();
    bool receive_attacks(std::queue<int>& attacks);
    void render_board(const char* label, int x_offset, bool show_controls, int attack_count, int anim_frame = 0) const;
    void place_viruses(int count);
    void new_piece();
    int simulate_cascade();
    void clone_grid(const PlayerBoard& src);
    void init(int virus_count, unsigned int seed);
    bool apply_move(Move m);
};
