/*
 * Dr. Mario Clone — Terminal Edition (2-Player vs Bot)
 * Compile: make
 * Run:     ./drmario
 * Controls: A/D = Move, S = Drop, W = Rotate, Q = Quit
 */

#include "board.h"
#include "bot_ai.h"
#include "terminal_io.h"

#include <cmath>
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>




// ======================  CONSTANTS  ======================
const int BOT_INPUT_TICK_RATE = 10;
const int GRAVITY_TICK_RATE = 20;
// ====================== GAME STATE ======================
static int GAME_FPS = 60;


static PlayerBoard player;
static PlayerBoard bot;
static BotState bot_state;
static std::queue<int> player_attacks; // attacks TO player (from bot)
static std::queue<int> bot_attacks;    // attacks TO bot (from player)
static float drop_speed = 24;
static int anim_frame = 0;
static pid_t music_pid = 0;
static bool game_over = false;
static int ticks = 0;
// ====================== HELPERS ======================

void new_piece_with_speed(PlayerBoard& board) {
    board.new_piece();
    drop_speed = std::fmax(5, drop_speed - 1.0/6.0); // 1/6 tick increment
}

// ====================== INPUT ======================

// Drain all pending keys from the input buffer. Returns QUIT if Q was pressed.
Move get_player_drain_input_check_quit() {
    while (true) {
        int ch = poll_key();
        if (ch == 0) return Move::NONE;
        if (ch == 'q' || ch == 'Q') return Move::QUIT;
    }
}

// Poll keyboard and return the player's move choice.
Move get_player_move() {
    Move move = Move::NONE;
    while (true) {
        int ch = poll_key();
        if (ch == 0) break;

        if (ch == 'q' || ch == 'Q') { return Move::QUIT; }

        switch (ch) {
        case 'a': case 'A': move = Move::LEFT;   break;
        case 'd': case 'D': move = Move::RIGHT;  break;
        case 's': case 'S': move = Move::DROP;   break;
        case 'w': case 'W': move = Move::ROTATE; break;
        }
    }
    return move;
}

void input_sleep(){
    // to avoid hot-looping
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

// ====================== PHASE TRANSITIONS ======================

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

// Unified phase processing for both player and bot.
// Returns true if game ended (game_won or game_over).
bool process_phases(PlayerBoard& board, std::queue<int>& my_attacks, std::queue<int>& opponent_attacks,
                    int& last_drop, int& last_gravity, bool is_player) {
    switch (board.phase) {
    case Phase::PLAYING: {
        // Check win
        if (board.cleared_viruses >= board.total_viruses) {
            board.game_won = true;
            return true;
        }

        if (ticks - last_drop <= std::ceil(drop_speed))
            // Drop speed throttle
            return false;

        last_drop = ticks;
        Capsule t = board.cap;
        t.r++;
        if (board.fits(t)) {
            board.cap = t;
            return false;
        }

        // Piece landed
        board.stamp(board.cap);

        // Check for matches first
        if (board.find_and_remove_matches() > 0) {
            board.phase = Phase::GRAVITY;
            last_gravity = ticks;
            return false;
        }

        // Receive attacks if queued
        if (!my_attacks.empty()) {
            if (!board.receive_attacks(my_attacks)) {
                // cant place attack, you lose.
                board.game_over = true;
                return true;
            }
            board.phase = Phase::GRAVITY;
            last_gravity = ticks;
            return false;
        }

        // Spawn next piece
        new_piece_with_speed(board);
        return false;
    }

    case Phase::GRAVITY: {
        if (ticks - last_gravity < GRAVITY_TICK_RATE)
            return false;

        last_gravity = ticks;
        if (board.gravity_step()) return false;

        // Gravity settled, check for new matches
        if (board.find_and_remove_matches() > 0) return false;

        // No more cascades — flush accumulated colors if 2+ cleared
        board.flush_cascade(opponent_attacks);

        if (board.cleared_viruses >= board.total_viruses) {
            board.game_won = true;
        } else {
            board.phase = Phase::PLAYING;
            new_piece_with_speed(board);
            last_drop = ticks;
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
                input_sleep();
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

    int drop_speed_int;
    if (!select_option("", {{'1', 40}, {'2', 24}, {'3', 12}}, drop_speed_int)) {
        std::cout << "\033[2J\033[H";
        return 0;
    }
    drop_speed = drop_speed_int;

    // ---- start music ----
    music_pid = fork();
    if (music_pid == 0) {
        while (true) {
            execlp("afplay", "afplay", "queque.mp3", nullptr);
            _exit(1);
        }
    }

    // ---- init ----
    player.init(nv);
    bot.init(nv);

    int player_last_drop = 0;
    int player_last_gravity = 0;
    int bot_last_drop = 0;
    int bot_last_gravity = 0;
    int bot_last_move = 0;

    // ---- game loop ----
    while (!(player.game_over || player.game_won || bot.game_won || bot.game_over)) {
        // ====== GATHER INPUTS ======
        Move player_move = Move::NONE;
        if (player.phase == Phase::PLAYING) {
            player_move = get_player_move();
        } else {
            player_move = get_player_drain_input_check_quit();
        }
        if (player_move == Move::QUIT) break;

        Move bot_move = Move::NONE;
        if (bot.phase == Phase::PLAYING && ((ticks - bot_last_move) >= BOT_INPUT_TICK_RATE)) {
            bot_last_move = ticks;
            bot_move = get_bot_move(bot, bot_state);
        }

        // ====== APPLY MOVES ======
        if (player.phase == Phase::PLAYING) apply_move(player, player_move);
        if (bot.phase == Phase::PLAYING)     apply_move(bot, bot_move);

        // ====== PHASE PROCESSING ======
        process_phases(player, player_attacks, bot_attacks,
                       player_last_drop, player_last_gravity, true);
        process_phases(bot, bot_attacks, player_attacks,
                       bot_last_drop, bot_last_gravity, false);
        render();
        ticks++;
        std::this_thread::sleep_for(std::chrono::milliseconds(((int)std::ceilf(1000.0/GAME_FPS))));
    }

    // stop music
    if (music_pid > 0) kill(music_pid, SIGTERM);

    render();
    std::cout << "\nPress 'q' to exit...\n";
    while (poll_key() != 'q') {
        input_sleep();
    }
    return 0;
}
