/*
 * Dr. Mario Clone — Terminal Edition (2-Player vs Bot)
 * Compile: make
 * Run:     ./drmario
 * Controls: A/D = Move, S = Drop, W = Rotate, Q = Quit
 */

#include "types.h"
#include "bot_ai.h"

#include <iostream>
#include <string>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <thread>
#include <termios.h>
#include <unistd.h>
#include <poll.h>
#include <sys/wait.h>
#include <signal.h>

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



// ====================== GAME STATE ======================

static PlayerBoard player;
static PlayerBoard bot;
static BotState bot_state;
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

// ====================== PHASE TRANSITIONS ======================

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

// Unified phase processing for both player and bot.
// Returns true if game ended (game_won or game_over).
bool process_phases(PlayerBoard& board, std::queue<int>& my_attacks, std::queue<int>& opponent_attacks,
                    TimePoint& last_drop, TimePoint& last_gravity, bool is_player) {
    switch (board.phase) {
    case Phase::PLAYING: {
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
        if (is_player) drain_input();

        // Check for matches first
        if (board.find_and_remove_matches(opponent_attacks) > 0) {
            board.phase = Phase::GRAVITY;
            last_gravity = Clock::now();
            return false;
        }

        // Check win
        if (board.cleared_viruses >= board.total_viruses) {
            board.game_won = true;
            return true;
        }

        // Receive attacks if queued
        if (!my_attacks.empty()) {
            if (!board.receive_attacks(my_attacks)) {
                board.game_over = true;
                return true;
            }
            board.phase = Phase::GRAVITY;
            last_gravity = Clock::now();
            return false;
        }

        // Spawn next piece
        new_piece(board);
        return false;
    }

    case Phase::GRAVITY: {
        auto now = Clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_gravity).count() < 250)
            return false;

        last_gravity = now;
        if (board.gravity_step()) return false;

        // Gravity settled, check for new matches
        if (board.find_and_remove_matches(opponent_attacks) > 0) return false;

        if (board.cleared_viruses >= board.total_viruses) {
            board.game_won = true;
        } else {
            board.phase = Phase::PLAYING;
            new_piece(board);
            last_drop = Clock::now();
        }
        return false;
    }
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
        process_phases(player, player_attacks, bot_attacks,
                       player_last_drop, player_last_gravity, true);

        // ====== BOT AI MOVE ======
        if (bot.phase == Phase::PLAYING &&
            std::chrono::duration_cast<std::chrono::milliseconds>(now - bot_last_move).count() >= 100) {
            bot_last_move = now;
            bot_ai_move(bot, bot_state);
        }

        // ====== BOT PHASE PROCESSING ======
        process_phases(bot, bot_attacks, player_attacks,
                       bot_last_drop, bot_last_gravity, false);

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
