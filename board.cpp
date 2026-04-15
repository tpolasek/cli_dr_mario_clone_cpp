#include "board.h"

// ====================== PLAYER BOARD METHODS ======================

int PlayerBoard::board_rand() {
    unsigned int state = rng_state;
    rng_state = rng_state * RNG_MULT + RNG_INC;
    return (int)(((state >> (state >> 29u)) ^ state) >> 18u);
}

int PlayerBoard::board_rnd_color() { return 1 + board_rand() % 3; }

void PlayerBoard::board_spawn(Capsule& c) {
    c.h1 = board_rnd_color();
    c.h2 = board_rnd_color();
    c.c = COLS / 2 - 1;
    c.r = 0;
    c.orient = 0;
}

void PlayerBoard::clear_grid() {
    for (auto& row : grid)
        for (auto& p : row) { p.color = EMPTY; p.virus = false; p.capId = 0; }
    next_cap_id = 1;
}

void PlayerBoard::stamp(const Capsule& c) {
    int id = next_cap_id++;
    auto set_cell = [&](int r, int col, int color) {
        if (r >= 0 && r < ROWS && col >= 0 && col < COLS) {
            grid[r][col].color = color;
            grid[r][col].virus = false;
            grid[r][col].capId = id;
        }
    };
    set_cell(c.r1(), c.c1(), c.h1);
    set_cell(c.r2(), c.c2(), c.h2);
}

int PlayerBoard::find_and_remove_matches() {
    bool kill[ROWS][COLS] = {};

    auto check_runs = [&](bool horizontal) {
        int outer = horizontal ? ROWS : COLS;
        int inner = horizontal ? COLS : ROWS;
        for (int i = 0; i < outer; i++) {
            int run = 1;
            for (int j = 1; j <= inner; j++) {
                int r1 = horizontal ? i : j - 1, c1 = horizontal ? j - 1 : i;
                int r2 = horizontal ? i : j,     c2 = horizontal ? j : i;
                bool same = (j < inner &&
                             grid[r2][c2].color != EMPTY &&
                             grid[r2][c2].color == grid[r1][c1].color);
                if (same) {
                    run++;
                } else {
                    if (run >= MIN_RUN_LENGTH) {
                        cascade_colors.push_back(grid[r1][c1].color);
                        for (int k = j - run; k < j; k++) {
                            int kr = horizontal ? i : k;
                            int kc = horizontal ? k : i;
                            kill[kr][kc] = true;
                        }
                    }
                    run = 1;
                }
            }
        }
    };

    check_runs(true);
    check_runs(false);

    int removed = 0, virus_killed = 0;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (kill[r][c]) {
                if (grid[r][c].virus) virus_killed++;
                grid[r][c].color = EMPTY;
                grid[r][c].virus = false;
                grid[r][c].capId = 0;
                removed++;
            }

    if (removed) {
        cleared_viruses += virus_killed;
        score += removed * SCORE_PER_PIECE;
    }
    return removed;
}

void PlayerBoard::flush_cascade(std::queue<int>& opponent_attacks) {
    // Only chained attacks are considered as attacks (so >=2 cascade_colors)
    if (cascade_colors.size() >= MIN_CASCADE_ATTACK) {
        for (int color : cascade_colors)
            opponent_attacks.push(color);
    }
    cascade_colors.clear();
}

void PlayerBoard::clear_cell(int r, int c) {
    grid[r][c] = {EMPTY, false, 0};
}

bool PlayerBoard::gravity_step() {
    bool moved = false;

    for (int r = ROWS - 2; r >= 0; r--) {
        for (int c = 0; c < COLS; c++) {
            if (grid[r][c].color == EMPTY || grid[r][c].virus)
                continue;

            int capId = grid[r][c].capId;

            // Check if this is the bottom half of a vertical capsule
            if (is_partner(r, c, -1, 0, capId)) {
                if (r + 1 < ROWS && grid[r + 1][c].color == EMPTY) {
                    grid[r + 1][c] = grid[r][c];
                    grid[r][c] = grid[r - 1][c];
                    clear_cell(r - 1, c);
                    moved = true;
                }
                continue;
            }

            // Check if this is the top half of a vertical capsule (skip, bottom handles it)
            if (is_partner(r, c, 1, 0, capId))
                continue;

            // Check for horizontal partner
            int dc = 0;
            if (is_partner(r, c, 0, -1, capId)) dc = -1;
            else if (is_partner(r, c, 0, 1, capId)) dc = 1;

            if (dc != 0) {
                int c2 = c + dc;
                if (r + 1 < ROWS &&
                    grid[r + 1][c].color == EMPTY &&
                    grid[r + 1][c2].color == EMPTY) {
                    grid[r + 1][c] = grid[r][c];
                    grid[r + 1][c2] = grid[r][c2];
                    clear_cell(r, c);
                    clear_cell(r, c2);
                    moved = true;
                }
            } else {
                // Single cell (orphan capsule half)
                if (r + 1 < ROWS && grid[r + 1][c].color == EMPTY) {
                    grid[r + 1][c] = grid[r][c];
                    clear_cell(r, c);
                    moved = true;
                }
            }
        }
    }
    return moved;
}

bool PlayerBoard::receive_attacks(std::queue<int>& attacks) {
    if (attacks.empty()) return true;

    int count = std::min((int)attacks.size(), MAX_ATTACKS);

    // Collect available columns (row 0 must be empty)
    std::vector<int> available;
    for (int c = 0; c < COLS; c++)
        if (grid[0][c].color == EMPTY)
            available.push_back(c);

    // Shuffle available columns
    for (int i = available.size() - 1; i > 0; i--) {
        // Intentionally use std::rand because attacks sent are not deterministic
        int j = std::rand() % (i + 1);
        std::swap(available[i], available[j]);
    }

    // Pick non-adjacent columns from the shuffled list
    std::vector<int> chosen;
    for (int c : available) {
        bool adjacent = false;
        for (int cc : chosen)
            if (std::abs(c - cc) == 1) { adjacent = true; break; }
        if (!adjacent) {
            chosen.push_back(c);
            if ((int)chosen.size() == count) break;
        }
    }

    // Not enough non-adjacent slots
    if ((int)chosen.size() < count) return false;

    for (int c : chosen) {
        grid[0][c].color = attacks.front();
        grid[0][c].virus = false;
        grid[0][c].capId = 0;
        attacks.pop();
    }

    // Discard any excess attacks
    while (!attacks.empty()) attacks.pop();

    return true;
}

void PlayerBoard::place_viruses(int count) {
    count = std::min(count, (ROWS - VIRUS_FREE_ROWS) * COLS);  // guard against infinite loop
    int placed = 0;
    while (placed < count) {
        int r = VIRUS_FREE_ROWS + board_rand() % (ROWS - VIRUS_FREE_ROWS);
        int c = board_rand() % COLS;
        if (grid[r][c].color == EMPTY) {
            grid[r][c].color = board_rnd_color();
            grid[r][c].virus = true;
            grid[r][c].capId = 0;
            placed++;
        }
    }
    total_viruses = count;
    cleared_viruses = 0;
}

void PlayerBoard::new_piece() {
    cap = nxt;
    board_spawn(nxt);
    if (!fits(cap)) game_over = true;
}

// Simulate full cascade: remove matches, gravity, repeat.
// Returns total viruses cleared across all cascades.
int PlayerBoard::simulate_cascade() {
    int total = 0;
    while (true) {
        int before = cleared_viruses;
        int removed = find_and_remove_matches();
        total += (cleared_viruses - before);
        if (removed == 0 && !gravity_step())
            break;
    }
    return total;
}

// Simulation-only version: skips cascade_colors tracking (not needed during bot search)
int PlayerBoard::find_and_remove_matches_sim() {
    bool kill[ROWS][COLS] = {};

    auto check_runs = [&](bool horizontal) {
        int outer = horizontal ? ROWS : COLS;
        int inner = horizontal ? COLS : ROWS;
        for (int i = 0; i < outer; i++) {
            int run = 1;
            for (int j = 1; j <= inner; j++) {
                int r1 = horizontal ? i : j - 1, c1 = horizontal ? j - 1 : i;
                int r2 = horizontal ? i : j,     c2 = horizontal ? j : i;
                bool same = (j < inner &&
                             grid[r2][c2].color != EMPTY &&
                             grid[r2][c2].color == grid[r1][c1].color);
                if (same) {
                    run++;
                } else {
                    if (run >= MIN_RUN_LENGTH) {
                        for (int k = j - run; k < j; k++) {
                            int kr = horizontal ? i : k;
                            int kc = horizontal ? k : i;
                            kill[kr][kc] = true;
                        }
                    }
                    run = 1;
                }
            }
        }
    };

    check_runs(true);
    check_runs(false);

    int removed = 0, virus_killed = 0;
    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            if (kill[r][c]) {
                if (grid[r][c].virus) virus_killed++;
                grid[r][c].color = EMPTY;
                grid[r][c].virus = false;
                grid[r][c].capId = 0;
                removed++;
            }

    if (removed) {
        cleared_viruses += virus_killed;
        score += removed * SCORE_PER_PIECE;
    }
    return removed;
}

int PlayerBoard::simulate_cascade_sim() {
    int total = 0;
    while (true) {
        int before = cleared_viruses;
        int removed = find_and_remove_matches_sim();
        total += (cleared_viruses - before);
        if (removed == 0 && !gravity_step())
            break;
    }
    return total;
}

// Copy only the grid state from another board (for fast simulation).
void PlayerBoard::clone_grid(const PlayerBoard& src) {
    grid = src.grid;
}

void PlayerBoard::init(int virus_count, unsigned int seed) {
    clear_grid();
    score = 0;
    game_over = false;
    game_won = false;
    phase = Phase::PLAYING;
    rng_state = seed;
    place_viruses(virus_count);
    board_spawn(nxt);
    new_piece();
}

// ====================== MOVE IMPLEMENTATION ======================

bool PlayerBoard::apply_move(Move m) {
    if (phase != Phase::PLAYING || m == Move::NONE) return false;
    Capsule t = cap;
    switch (m) {
    case Move::LEFT:   t.c--; break;
    case Move::RIGHT:  t.c++; break;
    case Move::DOWN:
    case Move::DROP:
        t.r++;
        break;
    case Move::ROTATE:
        t.rotate();
        if (fits(t)) { cap = t; return true; }
        t.c--; if (fits(t)) { cap = t; return true; }
        t.c += 2; if (fits(t)) { cap = t; return true; }
        t.c--; t.r--; if (fits(t)) { cap = t; return true; }
        return false;
    default: return false;
    }
    if (fits(t)) { cap = t; return true; }
    return false;
}
