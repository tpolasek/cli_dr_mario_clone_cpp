/*
 * Dr. Mario Clone — Terminal Edition
 * Compile: g++ -std=c++17 -O2 -o drmario drmario.cpp
 * Run:     ./drmario
 * Controls: A/D = Move, S = Drop, W = Rotate, Q = Quit
 */

#include <iostream>
#include <vector>
#include <array>
#include <string>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <thread>
#include <algorithm>
#include <termios.h>
#include <unistd.h>
#include <poll.h>

// ====================== CONSTANTS ======================

constexpr int ROWS = 16;
constexpr int COLS  = 8;
constexpr int EMPTY = 0;
constexpr int RED   = 1;
constexpr int YELLOW = 2;
constexpr int BLUE  = 3;

// ====================== TERMINAL I/O ======================

static struct termios orig_termios;

void disable_raw_mode() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    std::cout << "\033[?25h";
    std::cout << "\033[?1049l";
    std::cout.flush();
}

void enable_raw_mode() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disable_raw_mode);
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN]  = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int poll_key() {
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    if (poll(&pfd, 1, 0) > 0) {
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1)
            return static_cast<unsigned char>(c);
    }
    return 0;
}

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
        if (orient == 1) return r - 1;
        if (orient == 3) return r + 1;
        return r;
    }
    int c2() const {
        if (orient == 0) return c + 1;
        if (orient == 2) return c - 1;
        return c;
    }
    void rotate() { orient = (orient + 3) & 3; }
};

enum class Phase { PLAYING, GRAVITY };

// ====================== GAME STATE ======================

static std::array<std::array<Piece, COLS>, ROWS> grid;
static Capsule cap, nxt;
static int score = 0;
static int level = 1;
static int total_viruses = 0;
static int cleared_viruses = 0;
static bool game_over = false;
static bool game_won = false;
static Phase phase = Phase::PLAYING;
static int next_cap_id = 1;

// ====================== HELPERS ======================

int rnd_color() { return 1 + std::rand() % 3; }

bool in_bounds(int r, int c) {
    return r >= 0 && r < ROWS && c >= 0 && c < COLS;
}

bool cell_free(int r, int c) {
    if (r < 0 && c >= 0 && c < COLS) return true;
    return in_bounds(r, c) && grid[r][c].color == EMPTY;
}

// ====================== GRID OPS ======================

void clear_grid() {
    for (auto& row : grid)
        for (auto& p : row) { p.color = EMPTY; p.virus = false; p.capId = 0; }
    next_cap_id = 1;
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

void spawn(Capsule& c) {
    c.h1 = rnd_color();
    c.h2 = rnd_color();
    c.c = COLS / 2 - 1;
    c.r = 0;
    c.orient = 0;
}

bool fits(const Capsule& c) {
    return cell_free(c.r1(), c.c1()) && cell_free(c.r2(), c.c2());
}

void stamp(const Capsule& c) {
    int id = next_cap_id++;
    if (in_bounds(c.r1(), c.c1())) {
        grid[c.r1()][c.c1()].color = c.h1;
        grid[c.r1()][c.c1()].virus = false;
        grid[c.r1()][c.c1()].capId = id;
    }
    if (in_bounds(c.r2(), c.c2())) {
        grid[c.r2()][c.c2()].color = c.h2;
        grid[c.r2()][c.c2()].virus = false;
        grid[c.r2()][c.c2()].capId = id;
    }
}

// ====================== MATCH / GRAVITY ======================

int find_and_remove_matches() {
    std::vector<std::vector<bool>> kill(ROWS, std::vector<bool>(COLS, false));

    for (int r = 0; r < ROWS; r++) {
        int run = 1;
        for (int c = 1; c <= COLS; c++) {
            bool same = (c < COLS && grid[r][c].color != EMPTY &&
                         grid[r][c].color == grid[r][c - 1].color);
            if (same) { run++; }
            else {
                if (run >= 4)
                    for (int k = c - run; k < c; k++) kill[r][k] = true;
                run = 1;
            }
        }
    }
    for (int c = 0; c < COLS; c++) {
        int run = 1;
        for (int r = 1; r <= ROWS; r++) {
            bool same = (r < ROWS && grid[r][c].color != EMPTY &&
                         grid[r][c].color == grid[r - 1][c].color);
            if (same) { run++; }
            else {
                if (run >= 4)
                    for (int k = r - run; k < r; k++) kill[k][c] = true;
                run = 1;
            }
        }
    }

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
    }
    return removed;
}

bool gravity_step() {
    bool moved = false;
    std::vector<std::vector<bool>> done(ROWS, std::vector<bool>(COLS, false));

    for (int r = ROWS - 1; r >= 0; r--) {
        for (int c = 0; c < COLS; c++) {
            if (grid[r][c].color == EMPTY || grid[r][c].virus || done[r][c])
                continue;

            // Check for intact vertical partner directly above
            bool has_vert_partner = false;
            if (r - 1 >= 0 &&
                grid[r - 1][c].color != EMPTY &&
                !grid[r - 1][c].virus &&
                grid[r][c].capId == grid[r - 1][c].capId) {
                has_vert_partner = true;
            }

            if (has_vert_partner) {
                // This cell is the bottom of a vertical pair
                // Move both together if space below
                if (r + 1 < ROWS && grid[r + 1][c].color == EMPTY) {
                    grid[r + 1][c] = grid[r][c];
                    grid[r][c] = grid[r - 1][c];
                    grid[r - 1][c] = {EMPTY, false, 0};
                    done[r + 1][c] = true;
                    done[r][c] = true;
                    moved = true;
                }
            } else {
                // Check for intact vertical partner directly below
                bool has_below = false;
                if (r + 1 < ROWS &&
                    grid[r + 1][c].color != EMPTY &&
                    !grid[r + 1][c].virus &&
                    grid[r][c].capId == grid[r + 1][c].capId) {
                    has_below = true;
                }

                if (has_below) {
                    // This cell is the top of a vertical pair
                    // Don't move it here — the bottom half handles it
                    continue;
                }

                // Check for horizontal partner — intact horizontal pair can't fall
                bool has_horiz_partner = false;
                int dc = 0;
                if (c - 1 >= 0 &&
                    grid[r][c - 1].color != EMPTY &&
                    !grid[r][c - 1].virus &&
                    grid[r][c].capId == grid[r][c - 1].capId) {
                    has_horiz_partner = true;
                    dc = -1;
                }
                if (c + 1 < COLS &&
                    grid[r][c + 1].color != EMPTY &&
                    !grid[r][c + 1].virus &&
                    grid[r][c].capId == grid[r][c + 1].capId) {
                    has_horiz_partner = true;
                    dc = 1;
                }

                if (has_horiz_partner) {
                    // Both halves of horizontal pair must fall together
                    int r1 = r, r2 = r;
                    int c1 = c, c2 = c + dc;
                    if (done[r1][c1] || done[r2][c2]) continue;
                    if (r1 + 1 < ROWS && r2 + 1 < ROWS &&
                        grid[r1 + 1][c1].color == EMPTY &&
                        grid[r2 + 1][c2].color == EMPTY) {
                        grid[r1 + 1][c1] = grid[r1][c1];
                        grid[r2 + 1][c2] = grid[r2][c2];
                        grid[r1][c1] = {EMPTY, false, 0};
                        grid[r2][c2] = {EMPTY, false, 0};
                        done[r1 + 1][c1] = true;
                        done[r2 + 1][c2] = true;
                        moved = true;
                    }
                } else {
                    // Orphaned cell — fall independently
                    if (r + 1 < ROWS && grid[r + 1][c].color == EMPTY) {
                        grid[r + 1][c] = grid[r][c];
                        grid[r][c] = {EMPTY, false, 0};
                        done[r + 1][c] = true;
                        moved = true;
                    }
                }
            }
        }
    }
    return moved;
}

// ====================== INPUT ======================

void handle_input() {
    while (true) {
        int ch = poll_key();
        if (ch == 0) break;

        Capsule t = cap;
        switch (ch) {
        case 'a': case 'A': t.c--; if (fits(t)) cap = t; break;
        case 'd': case 'D': t.c++; if (fits(t)) cap = t; break;
        case 's': case 'S': {
            while (true) {
                t = cap; t.r++;
                if (fits(t)) cap = t; else break;
            }
            break;
        }
        case 'w': case 'W': {
            t.rotate();
            if (fits(t)) { cap = t; break; }
            t.c--;  if (fits(t)) { cap = t; break; }
            t.c += 2; if (fits(t)) { cap = t; break; }
            t.c--;
            t.r--;  if (fits(t)) { cap = t; break; }
            break;
        }
        case 'q': case 'Q': game_over = true; return;
        }
    }
}

void drain_input() {
    while (poll_key() != 0) {}
}

// ====================== RENDERING ======================

const char* clr_ansi(int c) {
    switch (c) {
        case RED:    return "\033[91m";
        case YELLOW: return "\033[93m";
        case BLUE:   return "\033[94m";
        default:     return "\033[0m";
    }
}

const char* dark_ansi(int c) {
    switch (c) {
        case RED:    return "\033[31m";
        case YELLOW: return "\033[33m";
        case BLUE:   return "\033[34m";
        default:     return "\033[0m";
    }
}

void render() {
    struct CellView { std::string txt; };
    std::vector<std::vector<CellView>> buf(ROWS, std::vector<CellView>(COLS));

    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            if (grid[r][c].color != EMPTY) {
                if (grid[r][c].virus)
                    buf[r][c].txt = std::string(dark_ansi(grid[r][c].color)) + "\u25C8\u25C8";
                else
                    buf[r][c].txt = std::string(clr_ansi(grid[r][c].color)) + "\u2588\u2588";
            } else {
                buf[r][c].txt = "  ";
            }
        }

    auto overlay = [&](int r, int c, int color) {
        if (in_bounds(r, c))
            buf[r][c].txt = std::string(clr_ansi(color)) + "\u2588\u2588";
    };
    if (phase == Phase::PLAYING) {
        overlay(cap.r1(), cap.c1(), cap.h1);
        overlay(cap.r2(), cap.c2(), cap.h2);
    }

    std::cout << "\033[H";
    std::cout << "\033[97;1m  DR. MARIO\033[0m\n\n";

    std::cout << "    \033[90m  .-----------.  \033[0m\n";
    std::cout << "    \033[90m  |           |  \033[0m\n";

    for (int r = 0; r < ROWS; r++) {
        std::cout << "    \033[90m  |\033[0m";
        for (int c = 0; c < COLS; c++)
            std::cout << buf[r][c].txt;
        std::cout << "\033[90m|\033[0m";

        if (r == 0)  std::cout << "   \033[90mScore:\033[0m " << score;
        if (r == 2)  std::cout << "   \033[90mLevel:\033[0m " << level;
        if (r == 4)  std::cout << "   \033[90mVirus:\033[0m "
                               << (total_viruses - cleared_viruses) << "/" << total_viruses;
        if (r == 7) {
            std::cout << "   \033[90mNext:\033[0m "
                      << clr_ansi(nxt.h1) << "\u2588\u2588"
                      << clr_ansi(nxt.h2) << "\u2588\033[0m";
        }
        if (r == 10) std::cout << "   \033[90mA/D  Move\033[0m";
        if (r == 11) std::cout << "   \033[90mW    Rotate\033[0m";
        if (r == 12) std::cout << "   \033[90mS    Drop\033[0m";
        if (r == 13) std::cout << "   \033[90mQ    Quit\033[0m";

        std::cout << "\033[0m\n";
    }

    std::cout << "    \033[90m  '-----------'  \033[0m\n";

    if (phase == Phase::GRAVITY)
        std::cout << "\n  \033[96m  Settling...\033[0m\n";
    else if (game_over)
        std::cout << "\n  \033[91;1m  G A M E   O V E R\033[0m\n";
    else if (game_won)
        std::cout << "\n  \033[92;1m  Y O U   W I N !\033[0m\n";
    else
        std::cout << "\n";

    std::cout.flush();
}

// ====================== MAIN ======================

int main() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    enable_raw_mode();
    std::cout << "\033[?1049h";
    std::cout << "\033[?25l";
    std::cout << "\033[2J\033[H";
    std::cout.flush();

    // ---- menu ----
    std::cout << "\n\n    \033[97;1m  DR. MARIO\033[0m  (Terminal Edition)\n\n";
    std::cout << "    Select virus count:\n";
    std::cout << "      \033[93m[1]\033[0m  Low    ( 5)\n";
    std::cout << "      \033[93m[2]\033[0m  Medium (10)\n";
    std::cout << "      \033[93m[3]\033[0m  High   (20)\n";
    std::cout << "      \033[93m[4]\033[0m  Ultra  (30)\n\n";
    std::cout << "    > ";
    std::cout.flush();

    int nv = 0;
    while (nv == 0) {
        int ch = poll_key();
        if (ch == '1') { nv = 5;  level = 1; }
        else if (ch == '2') { nv = 10; level = 2; }
        else if (ch == '3') { nv = 20; level = 3; }
        else if (ch == '4') { nv = 30; level = 4; }
        else if (ch == 'q' || ch == 'Q') {
            std::cout << "\033[2J\033[H";
            return 0;
        }
        if (nv == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // ---- init ----
    score = 0;
    game_over = false;
    game_won = false;
    phase = Phase::PLAYING;
    clear_grid();
    place_viruses(nv);
    spawn(nxt);

    auto new_piece = [&]() {
        cap = nxt;
        spawn(nxt);
        if (!fits(cap)) game_over = true;
    };
    new_piece();

    using Clock = std::chrono::steady_clock;
    auto last_drop = Clock::now();
    auto last_gravity = Clock::now();

    // ---- game loop ----
    while (!game_over && !game_won) {

        int speed = std::max(60, 380 - level * 50);
        auto now = Clock::now();

        // ====== PLAYING PHASE ======
        if (phase == Phase::PLAYING) {
            handle_input();
            if (game_over) break;

            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_drop).count() >= speed) {
                last_drop = now;

                Capsule t = cap; t.r++;
                if (fits(t)) {
                    cap = t;
                } else {
                    stamp(cap);
                    drain_input();

                    if (find_and_remove_matches() > 0) {
                        phase = Phase::GRAVITY;
                        last_gravity = Clock::now();
                    } else {
                        if (cleared_viruses >= total_viruses) {
                            game_won = true;
                        } else {
                            new_piece();
                        }
                    }
                }
            }
        }

        // ====== GRAVITY PHASE ======
        else if (phase == Phase::GRAVITY) {
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - last_gravity).count() >= speed) {
                last_gravity = now;

                if (gravity_step()) {
                    // pieces still falling — keep going
                } else {
                    if (find_and_remove_matches() > 0) {
                        // new chain matches — stay in gravity
                    } else {
                        if (cleared_viruses >= total_viruses) {
                            game_won = true;
                        } else {
                            phase = Phase::PLAYING;
                            new_piece();
                            last_drop = Clock::now();
                        }
                    }
                }
            }
        }

        render();
        std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }

    render();

    // wait for any key to exit
    while (true) {
        int ch = poll_key();
        if (ch != 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return 0;
}
