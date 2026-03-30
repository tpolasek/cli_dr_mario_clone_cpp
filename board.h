#pragma once

#include <array>
#include <vector>
#include <queue>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <iostream>

// ====================== CONSTANTS ======================

constexpr int ROWS = 16;
constexpr int COLS  = 8;
constexpr int EMPTY = 0;
constexpr int RED   = 1;
constexpr int YELLOW = 2;
constexpr int BLUE  = 3;

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

inline int rnd_color() { return 1 + std::rand() % 3; }

inline void spawn(Capsule& c) {
    c.h1 = rnd_color();
    c.h2 = rnd_color();
    c.c = COLS / 2 - 1;
    c.r = 0;
    c.orient = 0;
}

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

    void clear_grid() {
        for (auto& row : grid)
            for (auto& p : row) { p.color = EMPTY; p.virus = false; p.capId = 0; }
        next_cap_id = 1;
    }

    bool cell_free(int r, int c) const {
        if (r < 0 && c >= 0 && c < COLS) return true;
        return r >= 0 && r < ROWS && c >= 0 && c < COLS && grid[r][c].color == EMPTY;
    }

    bool fits(const Capsule& c) const {
        return cell_free(c.r1(), c.c1()) && cell_free(c.r2(), c.c2());
    }

    void stamp(const Capsule& c) {
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

    int find_and_remove_matches() {
        std::vector<std::vector<bool>> kill(ROWS, std::vector<bool>(COLS, false));
        std::vector<int> colors_cleared;

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
                        if (run >= 4) {
                            colors_cleared.push_back(grid[r1][c1].color);
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
            score += removed * 10;
            for (int color : colors_cleared)
                cascade_colors.push_back(color);
        }
        return removed;
    }

    void flush_cascade(std::queue<int>& opponent_attacks) {
        if (cascade_colors.size() >= 2) {
            for (int color : cascade_colors)
                opponent_attacks.push(color);
        }
        cascade_colors.clear();
    }

    bool is_partner(int r, int c, int dr, int dc, int capId) const {
        int nr = r + dr, nc = c + dc;
        return nr >= 0 && nr < ROWS && nc >= 0 && nc < COLS &&
               grid[nr][nc].color != EMPTY &&
               !grid[nr][nc].virus &&
               grid[nr][nc].capId == capId;
    }

    void swap_cells(int r1, int c1, int r2, int c2) {
        std::swap(grid[r1][c1], grid[r2][c2]);
    }

    void clear_cell(int r, int c) {
        grid[r][c] = {EMPTY, false, 0};
    }

    bool gravity_step() {
        bool moved = false;
        std::vector<std::vector<bool>> done(ROWS, std::vector<bool>(COLS, false));

        for (int r = ROWS - 1; r >= 0; r--) {
            for (int c = 0; c < COLS; c++) {
                if (grid[r][c].color == EMPTY || grid[r][c].virus || done[r][c])
                    continue;

                int capId = grid[r][c].capId;

                if (is_partner(r, c, -1, 0, capId)) {
                    if (r + 1 < ROWS && grid[r + 1][c].color == EMPTY) {
                        grid[r + 1][c] = grid[r][c];
                        grid[r][c] = grid[r - 1][c];
                        clear_cell(r - 1, c);
                        done[r + 1][c] = true;
                        done[r][c] = true;
                        moved = true;
                    }
                    continue;
                }

                if (is_partner(r, c, 1, 0, capId))
                    continue;

                int dc = 0;
                if (is_partner(r, c, 0, -1, capId)) dc = -1;
                else if (is_partner(r, c, 0, 1, capId)) dc = 1;

                if (dc != 0) {
                    int c2 = c + dc;
                    if (done[r][c] || done[r][c2]) continue;
                    if (r + 1 < ROWS &&
                        grid[r + 1][c].color == EMPTY &&
                        grid[r + 1][c2].color == EMPTY) {
                        grid[r + 1][c] = grid[r][c];
                        grid[r + 1][c2] = grid[r][c2];
                        clear_cell(r, c);
                        clear_cell(r, c2);
                        done[r + 1][c] = true;
                        done[r + 1][c2] = true;
                        moved = true;
                    }
                } else {
                    if (r + 1 < ROWS && grid[r + 1][c].color == EMPTY) {
                        grid[r + 1][c] = grid[r][c];
                        clear_cell(r, c);
                        done[r + 1][c] = true;
                        moved = true;
                    }
                }
            }
        }
        return moved;
    }

    bool receive_attacks(std::queue<int>& attacks) {
        if (attacks.empty()) return true;

        int count = std::min((int)attacks.size(), 4);

        // Collect available columns (row 0 must be empty)
        std::vector<int> available;
        for (int c = 0; c < COLS; c++)
            if (grid[0][c].color == EMPTY)
                available.push_back(c);

        // Shuffle available columns
        for (int i = available.size() - 1; i > 0; i--) {
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

    void render_board(const char* label, int x_offset, bool show_controls, int attack_count, int anim_frame = 0) const {
        struct CellView { std::string txt; };
        std::vector<std::vector<CellView>> buf(ROWS, std::vector<CellView>(COLS));

        const char* virus_char = (anim_frame & 32) ? "\u2742" : "\u2747";

        for (int r = 0; r < ROWS; r++)
            for (int c = 0; c < COLS; c++) {
                if (grid[r][c].color != EMPTY) {
                    if (grid[r][c].virus)
                        buf[r][c].txt = std::string(dark_ansi(grid[r][c].color)) + virus_char + virus_char;
                    else
                        buf[r][c].txt = std::string(clr_ansi(grid[r][c].color)) + "\u2588\u2588";
                } else {
                    buf[r][c].txt = "\033[90m ·\033[0m";
                }
            }

        auto overlay = [&](int r, int c, int color) {
            if (r >= 0 && r < ROWS && c >= 0 && c < COLS)
                buf[r][c].txt = std::string(clr_ansi(color)) + "\u2588\u2588";
        };
        if (phase == Phase::PLAYING) {
            overlay(cap.r1(), cap.c1(), cap.h1);
            overlay(cap.r2(), cap.c2(), cap.h2);
        }

        std::cout << "\033[" << 4 << ";" << (x_offset + 1) << "H";
        std::cout << "\033[97;1m" << label << "\033[0m";

        std::cout << "\033[" << 5 << ";" << x_offset << "H";
        std::cout << "\033[90m  .----------------.\033[0m";

        for (int r = 0; r < ROWS; r++) {
            std::cout << "\033[" << (6 + r) << ";" << x_offset << "H";
            std::cout << "\033[90m  |\033[0m";
            for (int c = 0; c < COLS; c++)
                std::cout << buf[r][c].txt;
            std::cout << "\033[90m|\033[0m";

            // Info panel
            if (r == 0)  std::cout << " \033[90mScore:\033[0m " << score;
            if (r == 2)  std::cout << " \033[90mVirus:\033[0m "
                                   << (total_viruses - cleared_viruses)
                                   << "/" << total_viruses;
            if (r == 4)  std::cout << " \033[90mNext:\033[0m "
                                   << clr_ansi(nxt.h1) << "\u2588\u2588"
                                   << clr_ansi(nxt.h2) << "\u2588\u2588\033[0m";
            if (r == 7)  std::cout << " \033[90mAttack:\033[0m " << attack_count;

            if (show_controls) {
                if (r == 9)  std::cout << " \033[90mA/D  Move\033[0m";
                if (r == 10) std::cout << " \033[90mW    Rotate\033[0m";
                if (r == 11) std::cout << " \033[90mS    Drop\033[0m";
                if (r == 12) std::cout << " \033[90mQ    Quit\033[0m";
            }
        }

        std::cout << "\033[" << (6 + ROWS) << ";" << x_offset << "H";
        std::cout << "\033[90m  '----------------'\033[0m";
    }

    void place_viruses(int count) {
        int placed = 0;
        while (placed < count) {
            int r = 4 + std::rand() % (ROWS - 4);
            int c = std::rand() % COLS;
            if (grid[r][c].color == EMPTY) {
                grid[r][c].color = rnd_color();
                grid[r][c].virus = true;
                grid[r][c].capId = 0;
                placed++;
            }
        }
        total_viruses = count;
        cleared_viruses = 0;
    }

    void new_piece() {
        cap = nxt;
        spawn(nxt);
        if (!fits(cap)) game_over = true;
    }

    // Simulate full cascade: remove matches, gravity, repeat.
    // Returns total viruses cleared across all cascades.
    int simulate_cascade() {
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

    // Copy only the grid state from another board (for fast simulation).
    void clone_grid(const PlayerBoard& src) {
        grid = src.grid;
    }

    void init(int virus_count) {
        clear_grid();
        score = 0;
        game_over = false;
        game_won = false;
        phase = Phase::PLAYING;
        place_viruses(virus_count);
        spawn(nxt);
        new_piece();
    }
};
