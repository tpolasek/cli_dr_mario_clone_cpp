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
        if (orient == 1 || orient == 3) return r - 1;
        return r;
    }
    int c2() const {
        if (orient == 0 || orient == 2) return c + 1;
        return c;
    }
    void rotate() {
        if ((orient & 1) == 0) std::swap(h1, h2);
        orient = (orient + 1) & 3;
    }
};

enum class Phase { PLAYING, GRAVITY, SENDING, RECEIVING };

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

    // Queue of attack pieces to add
    std::queue<int> attack_queue;

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
        if (c.r1() >= 0 && c.r1() < ROWS && c.c1() >= 0 && c.c1() < COLS) {
            grid[c.r1()][c.c1()].color = c.h1;
            grid[c.r1()][c.c1()].virus = false;
            grid[c.r1()][c.c1()].capId = id;
        }
        if (c.r2() >= 0 && c.r2() < ROWS && c.c2() >= 0 && c.c2() < COLS) {
            grid[c.r2()][c.c2()].color = c.h2;
            grid[c.r2()][c.c2()].virus = false;
            grid[c.r2()][c.c2()].capId = id;
        }
    }

    int find_and_remove_matches() {
        std::vector<std::vector<bool>> kill(ROWS, std::vector<bool>(COLS, false));

        // Track colors cleared for attacks
        std::vector<int> colors_cleared;

        for (int r = 0; r < ROWS; r++) {
            int run = 1;
            for (int c = 1; c <= COLS; c++) {
                bool same = (c < COLS && grid[r][c].color != EMPTY &&
                             grid[r][c].color == grid[r][c - 1].color);
                if (same) { run++; }
                else {
                    if (run >= 4) {
                        colors_cleared.push_back(grid[r][c - 1].color);
                        for (int k = c - run; k < c; k++) kill[r][k] = true;
                    }
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
                    if (run >= 4) {
                        colors_cleared.push_back(grid[r - 1][c].color);
                        for (int k = r - run; k < r; k++) kill[k][c] = true;
                    }
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

            // Add one attack piece per match group (don't deduplicate)
            for (int color : colors_cleared) {
                attack_queue.push(color);
            }
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

                bool has_vert_partner = false;
                if (r - 1 >= 0 &&
                    grid[r - 1][c].color != EMPTY &&
                    !grid[r - 1][c].virus &&
                    grid[r][c].capId == grid[r - 1][c].capId) {
                    has_vert_partner = true;
                }

                if (has_vert_partner) {
                    if (r + 1 < ROWS && grid[r + 1][c].color == EMPTY) {
                        grid[r + 1][c] = grid[r][c];
                        grid[r][c] = grid[r - 1][c];
                        grid[r - 1][c] = {EMPTY, false, 0};
                        done[r + 1][c] = true;
                        done[r][c] = true;
                        moved = true;
                    }
                } else {
                    bool has_below = false;
                    if (r + 1 < ROWS &&
                        grid[r + 1][c].color != EMPTY &&
                        !grid[r + 1][c].virus &&
                        grid[r][c].capId == grid[r + 1][c].capId) {
                        has_below = true;
                    }

                    if (has_below) continue;

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

    // Add all attack pieces at once at distinct random columns at row 0
    // Returns false if we can't place them (board full)
    bool receive_attacks() {
        if (attack_queue.empty()) return true;

        int count = attack_queue.size();

        // Get distinct random columns
        std::vector<int> cols;
        for (int c = 0; c < COLS; c++) cols.push_back(c);
        for (int i = COLS - 1; i > 0; i--) {
            int j = std::rand() % (i + 1);
            std::swap(cols[i], cols[j]);
        }

        // If we have more attacks than columns, we lose
        if (count > COLS) return false;

        // Check if any target column has no room at top
        for (int i = 0; i < count; i++) {
            int c = cols[i];
            if (grid[0][c].color != EMPTY) return false; // Column blocked at top
        }

        // Place all attack pieces at row 0
        for (int i = 0; i < count; i++) {
            int color = attack_queue.front();
            attack_queue.pop();
            int c = cols[i];

            grid[0][c].color = color;
            grid[0][c].virus = false;
            grid[0][c].capId = 0; // Orphaned piece
        }

        return true;
    }
};

// ====================== GAME STATE ======================

static PlayerBoard player;
static PlayerBoard bot;
static int level = 1;
static int drop_speed = 280;
static int anim_frame = 0;
static pid_t music_pid = 0;
static bool game_over = false;

// Bot AI state
static auto last_bot_move = std::chrono::steady_clock::now();


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
        case 's': case 'S': {
            t = player.cap; t.r++;
            if (player.fits(t)) player.cap = t;
            break;
        }
        case 'w': case 'W': {
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
}

void drain_input() {
    while (poll_key() != 0) {}
}

// ====================== BOT AI ======================

void bot_ai_move() {
    if (bot.phase != Phase::PLAYING) return;

    // Random AI: pick a random column and orientation, then drop
    static int target_col = -1;
    static int target_orient = -1;
    static bool moving_to_target = false;

    if (!moving_to_target) {
        // Pick new target
        target_col = std::rand() % (COLS - 1);
        target_orient = std::rand() % 4;
        moving_to_target = true;
    }

    // Move toward target
    Capsule t = bot.cap;

    // First orient
    if (bot.cap.orient != target_orient) {
        t = bot.cap;
        t.rotate();
        if (bot.fits(t)) {
            bot.cap = t;
            return;
        }
    }

    // Then move column
    if (bot.cap.c < target_col) {
        t = bot.cap;
        t.c++;
        if (bot.fits(t)) {
            bot.cap = t;
            return;
        }
    } else if (bot.cap.c > target_col) {
        t = bot.cap;
        t.c--;
        if (bot.fits(t)) {
            bot.cap = t;
            return;
        }
    }

    // At target, drop faster
    moving_to_target = false;
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

void render_board(const PlayerBoard& board, const char* label, int x_offset, bool show_controls) {
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

    // Move cursor to position
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

        // Info on the right side of the board
        if (show_controls) {
            if (r == 0)  std::cout << " \033[90mScore:\033[0m " << board.score;
            if (r == 2)  std::cout << " \033[90mVirus:\033[0m "
                                   << (board.total_viruses - board.cleared_viruses)
                                   << "/" << board.total_viruses;
            if (r == 4) {
                std::cout << " \033[90mNext:\033[0m "
                          << clr_ansi(board.nxt.h1) << "\u2588\u2588"
                          << clr_ansi(board.nxt.h2) << "\u2588\u2588\033[0m";
            }
            if (r == 7)  std::cout << " \033[90mAttack:\033[0m " << board.attack_queue.size();
            if (r == 9)  std::cout << " \033[90mA/D  Move\033[0m";
            if (r == 10) std::cout << " \033[90mW    Rotate\033[0m";
            if (r == 11) std::cout << " \033[90mS    Drop\033[0m";
            if (r == 12) std::cout << " \033[90mQ    Quit\033[0m";
        } else {
            if (r == 0)  std::cout << " \033[90mScore:\033[0m " << board.score;
            if (r == 2)  std::cout << " \033[90mVirus:\033[0m "
                                   << (board.total_viruses - board.cleared_viruses)
                                   << "/" << board.total_viruses;
            if (r == 4) {
                std::cout << " \033[90mNext:\033[0m "
                          << clr_ansi(board.nxt.h1) << "\u2588\u2588"
                          << clr_ansi(board.nxt.h2) << "\u2588\u2588\033[0m";
            }
            if (r == 7)  std::cout << " \033[90mAttack:\033[0m " << board.attack_queue.size();
        }
    }

    std::cout << "\033[" << (6 + ROWS) << ";" << x_offset << "H";
    std::cout << "\033[90m  '----------------'\033[0m";
}

void render() {
    anim_frame++;

    // Clear screen
    std::cout << "\033[2J\033[H";

    // Title
    std::cout << "\033[97;1m                    DR. MARIO — VS BOT\033[0m\n\n";

    // Render both boards side by side
    render_board(player, "PLAYER", 2, true);
    render_board(bot, "  BOT", 38, false);

    // Status line
    int status_row = 6 + ROWS + 2;
    std::cout << "\033[" << status_row << ";1H";

    if (player.game_over) {
        std::cout << "\033[91;1m         YOU LOSE! Bot wins!\033[0m";
    } else if (bot.game_over) {
        std::cout << "\033[92;1m         YOU WIN! Bot lost!\033[0m";
    } else if (player.phase == Phase::GRAVITY || bot.phase == Phase::GRAVITY) {
        std::cout << "\033[96m                      Settling...\033[0m";
    } else if (player.phase == Phase::RECEIVING) {
        std::cout << "\033[91m                   Receiving attack!\033[0m";
    } else if (bot.phase == Phase::RECEIVING) {
        std::cout << "\033[92m                    Sending attack!\033[0m";
    } else {
        std::cout << "                      ";
    }

    std::cout << "\033[" << (status_row + 2) << ";1H";
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
    std::cout << "\n\n    \033[97;1m  DR. MARIO — VS BOT\033[0m  (Terminal Edition)\n\n";
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
        if (ch == '1') nv = 5;
        else if (ch == '2') nv = 10;
        else if (ch == '3') nv = 20;
        else if (ch == '4') nv = 30;
        else if (ch == 'q' || ch == 'Q') {
            std::cout << "\033[2J\033[H";
            return 0;
        }
        if (nv == 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    // ---- speed selection ----
    std::cout << "\n    Select speed:\n";
    std::cout << "      \033[93m[1]\033[0m  Low\n";
    std::cout << "      \033[93m[2]\033[0m  Medium\n";
    std::cout << "      \033[93m[3]\033[0m  High\n\n";
    std::cout << "    > ";
    std::cout.flush();

    drop_speed = 380;
    while (drop_speed == 380) {
        int ch = poll_key();
        if (ch == '1') drop_speed = 480;
        else if (ch == '2') drop_speed = 280;
        else if (ch == '3') drop_speed = 140;
        else if (ch == 'q' || ch == 'Q') {
            std::cout << "\033[2J\033[H";
            return 0;
        }
        if (drop_speed == 380)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
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

    player.clear_grid();
    player.score = 0;
    player.game_over = false;
    player.game_won = false;
    player.phase = Phase::PLAYING;
    place_viruses(player, nv);
    spawn(player.nxt);
    new_piece(player);

    bot.clear_grid();
    bot.score = 0;
    bot.game_over = false;
    bot.game_won = false;
    bot.phase = Phase::PLAYING;
    place_viruses(bot, nv);
    spawn(bot.nxt);
    new_piece(bot);

    using Clock = std::chrono::steady_clock;
    auto player_last_drop = Clock::now();
    auto player_last_gravity = Clock::now();
    auto bot_last_drop = Clock::now();
    auto bot_last_gravity = Clock::now();
    auto bot_last_move = Clock::now();

    // ---- game loop ----
    while (!game_over && !player.game_over && !bot.game_over) {
        auto now = Clock::now();

        // ====== PLAYER INPUT ======
        handle_player_input();
        if (game_over) break;

        // ====== PLAYER PIECE DROP ======
        if (player.phase == Phase::PLAYING) {
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - player_last_drop).count() >= drop_speed) {
                player_last_drop = now;

                Capsule t = player.cap; t.r++;
                if (player.fits(t)) {
                    player.cap = t;
                } else {
                    player.stamp(player.cap);
                    drain_input();

                    if (player.find_and_remove_matches() > 0) {
                        player.phase = Phase::GRAVITY;
                        player_last_gravity = Clock::now();
                    } else {
                        if (player.cleared_viruses >= player.total_viruses) {
                            player.game_won = true;
                        } else {
                            new_piece(player);
                        }
                    }
                }
            }
        }

        // ====== PLAYER GRAVITY ======
        else if (player.phase == Phase::GRAVITY) {
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - player_last_gravity).count() >= 250) {
                player_last_gravity = now;

                if (player.gravity_step()) {
                    // pieces still falling
                } else {
                    if (player.find_and_remove_matches() > 0) {
                        // new chain
                    } else {
                        // Check if we have attacks to send
                        if (!player.attack_queue.empty()) {
                            player.phase = Phase::SENDING;
                        } else if (player.cleared_viruses >= player.total_viruses) {
                            player.game_won = true;
                        } else {
                            player.phase = Phase::PLAYING;
                            new_piece(player);
                            player_last_drop = Clock::now();
                        }
                    }
                }
            }
        }

        // ====== PLAYER SENDING ATTACKS ======
        else if (player.phase == Phase::SENDING) {
            // Transfer all attacks to bot at once
            while (!player.attack_queue.empty()) {
                int color = player.attack_queue.front();
                player.attack_queue.pop();
                bot.attack_queue.push(color);
            }
            if (player.cleared_viruses >= player.total_viruses) {
                player.game_won = true;
            } else {
                player.phase = Phase::PLAYING;
                new_piece(player);
                player_last_drop = Clock::now();
            }
        }

        // ====== PLAYER RECEIVING (processing attacks) ======
        else if (player.phase == Phase::RECEIVING) {
            // Place all attack pieces at top, then let gravity handle falling
            if (!player.receive_attacks()) {
                player.game_over = true;
            } else {
                player.phase = Phase::GRAVITY;
                player_last_gravity = Clock::now();
            }
        }

        // ====== BOT AI MOVE ======
        if (bot.phase == Phase::PLAYING) {
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - bot_last_move).count() >= 100) {
                bot_last_move = now;
                bot_ai_move();
            }
        }

        // ====== BOT PIECE DROP ======
        if (bot.phase == Phase::PLAYING) {
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - bot_last_drop).count() >= drop_speed) {
                bot_last_drop = now;

                Capsule t = bot.cap; t.r++;
                if (bot.fits(t)) {
                    bot.cap = t;
                } else {
                    bot.stamp(bot.cap);

                    if (bot.find_and_remove_matches() > 0) {
                        bot.phase = Phase::GRAVITY;
                        bot_last_gravity = Clock::now();
                    } else {
                        if (bot.cleared_viruses >= bot.total_viruses) {
                            bot.game_won = true;
                        } else {
                            new_piece(bot);
                        }
                    }
                }
            }
        }

        // ====== BOT GRAVITY ======
        else if (bot.phase == Phase::GRAVITY) {
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - bot_last_gravity).count() >= 250) {
                bot_last_gravity = now;

                if (bot.gravity_step()) {
                    // pieces still falling
                } else {
                    if (bot.find_and_remove_matches() > 0) {
                        // new chain
                    } else {
                        // Check if bot has attacks to send
                        if (!bot.attack_queue.empty()) {
                            bot.phase = Phase::SENDING;
                        } else if (bot.cleared_viruses >= bot.total_viruses) {
                            bot.game_won = true;
                        } else {
                            bot.phase = Phase::PLAYING;
                            new_piece(bot);
                            bot_last_drop = Clock::now();
                        }
                    }
                }
            }
        }

        // ====== BOT SENDING ATTACKS ======
        else if (bot.phase == Phase::SENDING) {
            // Transfer all attacks to player at once
            while (!bot.attack_queue.empty()) {
                int color = bot.attack_queue.front();
                bot.attack_queue.pop();
                player.attack_queue.push(color);
            }
            if (bot.cleared_viruses >= bot.total_viruses) {
                bot.game_won = true;
            } else {
                bot.phase = Phase::PLAYING;
                new_piece(bot);
                bot_last_drop = Clock::now();
            }
        }

        // ====== BOT RECEIVING ======
        else if (bot.phase == Phase::RECEIVING) {
            // Place all attack pieces at top, then let gravity handle falling
            if (!bot.receive_attacks()) {
                bot.game_over = true;
            } else {
                bot.phase = Phase::GRAVITY;
                bot_last_gravity = Clock::now();
            }
        }

        // ====== CHECK FOR INCOMING ATTACKS ======
        // After any phase transition, check if attacks are incoming
        if (player.phase == Phase::PLAYING && !player.attack_queue.empty()) {
            player.phase = Phase::RECEIVING;
            player_last_gravity = Clock::now();
        }
        if (bot.phase == Phase::PLAYING && !bot.attack_queue.empty()) {
            bot.phase = Phase::RECEIVING;
            bot_last_gravity = Clock::now();
        }

        render();
        std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }

    render();

    // stop music
    if (music_pid > 0) kill(music_pid, SIGTERM);

    // wait for any key to exit
    while (true) {
        int ch = poll_key();
        if (ch != 0) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return 0;
}
