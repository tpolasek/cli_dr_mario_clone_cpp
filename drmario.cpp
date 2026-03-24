/*
 * Dr. Mario Clone — Terminal Edition (2-Player vs Bot)
 * Compile: make
 * Run:     ./drmario
 * Controls: A/D = Move, S = Drop, W = Rotate, Q = Quit
 */

#include "board.h"
#include "bot_ai.h"
#include "terminal_io.h"

#include <iostream>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>



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

void new_piece_with_speed(PlayerBoard& board) {
    board.new_piece();
    drop_speed = std::max(60, drop_speed - 2);
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
        new_piece_with_speed(board);
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
            new_piece_with_speed(board);
            last_drop = Clock::now();
        }
        return false;
    }
    }
    return false;
}



// ====================== RENDERING ======================

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

    player.render_board("PLAYER", 2, true, player_attacks.size(), anim_frame);
    bot.render_board("  BOT", 38, false, bot_attacks.size(), anim_frame);

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
    player.init(nv);
    bot.init(nv);

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
