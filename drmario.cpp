/*
 * Dr. Mario Clone — Terminal Edition (2-Player vs Bot)
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
#include <queue>
#include <termios.h>
#include <unistd.h>
#include <poll.h>
#include <sys/wait.h>
#include <signal.h>

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

// ====================== PLAYER STATE ======================

struct PlayerBoard {
    std::array<std::array<Piece, COLS>, ROWS> grid;
    Capsule cap, nxt;
    int score = 0;
    int total_viruses = 0;
    int cleared_viruses = 0;
    int next_cap_id = 1;
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

    // Finds matches, removes them, and sends attack colors directly to opponent.
    // Returns number of cells removed.
    int find_and_remove_matches(std::queue<int>& opponent_attacks) {
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

        check_runs(true);  // horizontal
        check_runs(false); // vertical

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
                opponent_attacks.push(color);
        }
        return removed;
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

                // Check for vertical partner above
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

                // Check if we have a partner below (we're on top)
                if (is_partner(r, c, 1, 0, capId))
                    continue;

                // Check for horizontal partner
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
                    // Single piece or orphaned
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

    // Applies queued attacks to the top row. Returns false if no room (game over).
    bool receive_attacks(std::queue<int>& attacks) {
        if (attacks.empty()) return true;

        int count = attacks.size();
        if (count > COLS) return false;

        // Get distinct random columns
        std::vector<int> cols(COLS);
        for (int c = 0; c < COLS; c++) cols[c] = c;
        for (int i = COLS - 1; i > 0; i--) {
            int j = std::rand() % (i + 1);
            std::swap(cols[i], cols[j]);
        }

        // Check if target columns have room
        for (int i = 0; i < count; i++)
            if (grid[0][cols[i]].color != EMPTY) return false;

        // Place attack pieces
        for (int i = 0; i < count; i++) {
            int c = cols[i];
            grid[0][c].color = attacks.front();
            grid[0][c].virus = false;
            grid[0][c].capId = 0;
            attacks.pop();
        }
        return true;
    }
};

// ====================== GAME STATE ======================

static PlayerBoard player;
static PlayerBoard bot;
static std::queue<int> player_attacks; // attacks TO player (from bot)
static std::queue<int> bot_attacks;    // attacks TO bot (from player)
static int drop_speed = 280;
static int anim_frame = 0;
static pid_t music_pid = 0;
static bool game_over = false;

// ====================== HELPERS ======================

int rnd_color() { return 1 + std::rand() % 3; }

void place_viruses(PlayerBoard& board, int count) {
    int placed = 0;
    while (placed < count) {
        int r = 4 + std::rand() % (ROWS - 4);
        int c = std::rand() % COLS;
        if (board.grid[r][c].color == EMPTY) {
            board.grid[r][c].color = rnd_color();
            board.grid[r][c].virus = true;
            board.grid[r][c].capId = 0;
            placed++;
        }
    }
    board.total_viruses = count;
    board.cleared_viruses = 0;
}

void spawn(Capsule& c) {
    c.h1 = rnd_color();
    c.h2 = rnd_color();
    c.c = COLS / 2 - 1;
    c.r = 0;
    c.orient = 0;
}

void new_piece(PlayerBoard& board) {
    board.cap = board.nxt;
    spawn(board.nxt);
    drop_speed = std::max(60, drop_speed - 2);
    if (!board.fits(board.cap)) board.game_over = true;
}

void init_board(PlayerBoard& board, int virus_count) {
    board.clear_grid();
    board.score = 0;
    board.game_over = false;
    board.game_won = false;
    board.phase = Phase::PLAYING;
    place_viruses(board, virus_count);
    spawn(board.nxt);
    new_piece(board);
}

// ====================== PLAYER INPUT ======================

void handle_player_input() {
    while (true) {
        int ch = poll_key();
        if (ch == 0) break;

        if (ch == 'q' || ch == 'Q') {
            game_over = true;
            return;
        }

        if (player.phase != Phase::PLAYING) continue;

        Capsule t = player.cap;
        switch (ch) {
        case 'a': case 'A': t.c--; if (player.fits(t)) player.cap = t; break;
        case 'd': case 'D': t.c++; if (player.fits(t)) player.cap = t; break;
        case 's': case 'S':
            t.r++;
            if (player.fits(t)) player.cap = t;
            break;
        case 'w': case 'W':
            t.rotate();
            if (player.fits(t)) { player.cap = t; break; }
            t.c--;  if (player.fits(t)) { player.cap = t; break; }
            t.c += 2; if (player.fits(t)) { player.cap = t; break; }
            t.c--;
            t.r--;  if (player.fits(t)) { player.cap = t; break; }
            break;
        }
    }
}

void drain_input() {
    while (poll_key() != 0) {}
}

// ====================== BOT AI ======================

void bot_ai_move() {
    if (bot.phase != Phase::PLAYING) return;

    static int target_col = -1;
    static int target_orient = -1;
    static bool moving_to_target = false;

    if (!moving_to_target) {
        target_col = std::rand() % (COLS - 1);
        target_orient = std::rand() % 4;
        moving_to_target = true;
    }

    // Rotate toward target orientation
    if (bot.cap.orient != target_orient) {
        Capsule t = bot.cap;
        t.rotate();
        if (bot.fits(t)) { bot.cap = t; return; }
    }

    // Move toward target column
    if (bot.cap.c < target_col) {
        Capsule t = bot.cap; t.c++;
        if (bot.fits(t)) { bot.cap = t; return; }
    } else if (bot.cap.c > target_col) {
        Capsule t = bot.cap; t.c--;
        if (bot.fits(t)) { bot.cap = t; return; }
    }

    moving_to_target = false;
}

// ====================== PHASE TRANSITIONS ======================

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

// Returns true if piece dropped and landed (needs match check)
bool process_drop(PlayerBoard& board, TimePoint& last_drop) {
    auto now = Clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_drop).count() < drop_speed)
        return false;

    last_drop = now;
    Capsule t = board.cap;
    t.r++;
    if (board.fits(t)) {
        board.cap = t;
        return false;
    }

    // Piece landed
    board.stamp(board.cap);
    if (&board == &player) drain_input();
    return true;
}

// Handle gravity phase, returns true if still processing
bool process_gravity(PlayerBoard& board, std::queue<int>& opponent_attacks, TimePoint& last_gravity, TimePoint& last_drop) {
    auto now = Clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_gravity).count() < 250)
        return true;

    last_gravity = now;
    if (board.gravity_step()) return true;

    // Gravity settled, check for new matches (attacks go directly to opponent)
    if (board.find_and_remove_matches(opponent_attacks) > 0) return true;

    if (board.cleared_viruses >= board.total_viruses) {
        board.game_won = true;
    } else {
        board.phase = Phase::PLAYING;
        new_piece(board);
        last_drop = Clock::now();
    }
    return false;
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

void render_board(const PlayerBoard& board, const char* label, int x_offset, bool show_controls, int attack_count) {
    struct CellView { std::string txt; };
    std::vector<std::vector<CellView>> buf(ROWS, std::vector<CellView>(COLS));

    const char* virus_char = (anim_frame & 32) ? "\u2742" : "\u2747";

    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            if (board.grid[r][c].color != EMPTY) {
                if (board.grid[r][c].virus)
                    buf[r][c].txt = std::string(dark_ansi(board.grid[r][c].color)) + virus_char + virus_char;
                else
                    buf[r][c].txt = std::string(clr_ansi(board.grid[r][c].color)) + "\u2588\u2588";
            } else {
                buf[r][c].txt = "\033[90m ·\033[0m";
            }
        }

    auto overlay = [&](int r, int c, int color) {
        if (r >= 0 && r < ROWS && c >= 0 && c < COLS)
            buf[r][c].txt = std::string(clr_ansi(color)) + "\u2588\u2588";
    };
    if (board.phase == Phase::PLAYING) {
        overlay(board.cap.r1(), board.cap.c1(), board.cap.h1);
        overlay(board.cap.r2(), board.cap.c2(), board.cap.h2);
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
        if (r == 0)  std::cout << " \033[90mScore:\033[0m " << board.score;
        if (r == 2)  std::cout << " \033[90mVirus:\033[0m "
                               << (board.total_viruses - board.cleared_viruses)
                               << "/" << board.total_viruses;
        if (r == 4)  std::cout << " \033[90mNext:\033[0m "
                               << clr_ansi(board.nxt.h1) << "\u2588\u2588"
                               << clr_ansi(board.nxt.h2) << "\u2588\u2588\033[0m";
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

const char* status_text() {
    if (player.game_over) return "\033[91;1m         YOU LOSE! Bot wins!\033[0m";
    if (bot.game_over)    return "\033[92;1m         YOU WIN! Bot lost!\033[0m";
    if (player.phase == Phase::GRAVITY || bot.phase == Phase::GRAVITY)
        return "\033[96m                      Settling...\033[0m";
    return "                      ";
}

void render() {
    anim_frame++;
    std::cout << "\033[2J\033[H";
    std::cout << "\033[97;1m                    DR. MARIO — VS BOT\033[0m\n\n";

    render_board(player, "PLAYER", 2, true, player_attacks.size());
    render_board(bot, "  BOT", 38, false, bot_attacks.size());

    int status_row = 6 + ROWS + 2;
    std::cout << "\033[" << status_row << ";1H" << status_text();
    std::cout << "\033[" << (status_row + 2) << ";1H";
    std::cout.flush();
}

// ====================== MAIN ======================

int main() {
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    enable_raw_mode();
    std::cout << "\033[?1049h\033[?25l\033[2J\033[H";
    std::cout.flush();

    // ---- menu ----
    auto select_option = [](const char* prompt, std::initializer_list<std::pair<char, int>> options, int& result) -> bool {
        std::cout << prompt;
        std::cout.flush();
        result = 0;
        while (result == 0) {
            int ch = poll_key();
            if (ch == 'q' || ch == 'Q') return false;
            for (auto& [key, val] : options)
                if (ch == key) { result = val; break; }
            if (result == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return true;
    };

    std::cout << "\n\n    \033[97;1m  DR. MARIO — VS BOT\033[0m  (Terminal Edition)\n\n";

    int nv;
    if (!select_option(
        "    Select virus count:\n"
        "      \033[93m[1]\033[0m  Low    ( 5)\n"
        "      \033[93m[2]\033[0m  Medium (10)\n"
        "      \033[93m[3]\033[0m  High   (20)\n"
        "      \033[93m[4]\033[0m  Ultra  (30)\n\n    > ",
        {{'1', 5}, {'2', 10}, {'3', 20}, {'4', 30}},
        nv)) {
        std::cout << "\033[2J\033[H";
        return 0;
    }

    std::cout << "\n    Select speed:\n"
              << "      \033[93m[1]\033[0m  Low\n"
              << "      \033[93m[2]\033[0m  Medium\n"
              << "      \033[93m[3]\033[0m  High\n\n    > ";

    if (!select_option("", {{'1', 480}, {'2', 280}, {'3', 140}}, drop_speed)) {
        std::cout << "\033[2J\033[H";
        return 0;
    }

    // ---- start music ----
    music_pid = fork();
    if (music_pid == 0) {
        while (true) {
            execlp("afplay", "afplay", "queque.mp3", nullptr);
            _exit(1);
        }
    }

    // ---- init ----
    game_over = false;
    init_board(player, nv);
    init_board(bot, nv);

    TimePoint player_last_drop = Clock::now();
    TimePoint player_last_gravity = Clock::now();
    TimePoint bot_last_drop = Clock::now();
    TimePoint bot_last_gravity = Clock::now();
    TimePoint bot_last_move = Clock::now();

    // ---- game loop ----
    while (!game_over && !player.game_over && !bot.game_over) {
        auto now = Clock::now();

        // ====== PLAYER INPUT ======
        handle_player_input();
        if (game_over) break;

        // ====== PLAYER PHASE PROCESSING ======
        switch (player.phase) {
        case Phase::PLAYING:
            if (process_drop(player, player_last_drop)) {
                if (player.find_and_remove_matches(bot_attacks) > 0) {
                    player.phase = Phase::GRAVITY;
                    player_last_gravity = Clock::now();
                } else if (player.cleared_viruses >= player.total_viruses) {
                    player.game_won = true;
                } else if (!player_attacks.empty()) {
                    if (!player.receive_attacks(player_attacks)) {
                        player.game_over = true;
                        break;
                    }
                    player.phase = Phase::GRAVITY;
                    player_last_gravity = Clock::now();
                } else {
                    new_piece(player);
                }
            }
            break;

        case Phase::GRAVITY:
            process_gravity(player, bot_attacks, player_last_gravity, player_last_drop);
            break;
        }

        // ====== BOT AI MOVE ======
        if (bot.phase == Phase::PLAYING &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - bot_last_move).count() >= 100) {
            bot_last_move = now;
            bot_ai_move();
        }

        // ====== BOT PHASE PROCESSING ======
        switch (bot.phase) {
        case Phase::PLAYING:
            if (process_drop(bot, bot_last_drop)) {
                if (bot.find_and_remove_matches(player_attacks) > 0) {
                    bot.phase = Phase::GRAVITY;
                    bot_last_gravity = Clock::now();
                } else if (bot.cleared_viruses >= bot.total_viruses) {
                    bot.game_won = true;
                } else if (!bot_attacks.empty()) {
                    if (!bot.receive_attacks(bot_attacks)) {
                        bot.game_over = true;
                        break;
                    }
                    bot.phase = Phase::GRAVITY;
                    bot_last_gravity = Clock::now();
                } else {
                    new_piece(bot);
                }
            }
            break;

        case Phase::GRAVITY:
            process_gravity(bot, player_attacks, bot_last_gravity, bot_last_drop);
            break;
        }

        render();
        std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }

    render();

    // stop music
    if (music_pid > 0) kill(music_pid, SIGTERM);

    // wait for any key to exit
    while (poll_key() == 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

    return 0;
}
