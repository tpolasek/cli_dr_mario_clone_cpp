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

// ====================== GAME STATE ======================
static int game_fps = GAME_FPS;

struct Game {
    PlayerBoard player;
    PlayerBoard bot;
    BotState bot_state;
    std::queue<int> player_attacks; // attacks TO player (from bot)
    std::queue<int> bot_attacks;    // attacks TO bot (from player)
    float drop_speed = 24;
    int ticks = 0;
    int anim_frame = 0;
    pid_t music_pid = 0;
};

static Game game;
// ====================== HELPERS ======================

void new_piece_with_speed(PlayerBoard& board) {
    board.new_piece();
    game.drop_speed = std::fmax(5, game.drop_speed - 1.0/6.0); // 1/6 tick increment
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

        if (game.ticks - last_drop <= std::ceil(game.drop_speed))
            // Drop speed throttle
            return false;

        last_drop = game.ticks;
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
            last_gravity = game.ticks;
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
            last_gravity = game.ticks;
            return false;
        }

        // Spawn next piece
        new_piece_with_speed(board);
        return false;
    }

    case Phase::GRAVITY: {
        if (game.ticks - last_gravity < GRAVITY_TICK_RATE)
            return false;

        last_gravity = game.ticks;
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
            last_drop = game.ticks;
        }
        return false;
    }
    }
    return false;
}



// ====================== RENDERING ======================

const char* status_text() {
    if (game.player.game_over) return "\033[91;1m         YOU LOSE! Bot wins!\033[0m";
    if (game.bot.game_over)    return "\033[92;1m         YOU WIN! Bot lost!\033[0m";
    if (game.player.phase == Phase::GRAVITY || game.bot.phase == Phase::GRAVITY)
        return "\033[96m                      Settling...\033[0m";
    return "                      ";
}

void render() {
    game.anim_frame++;
    std::cout << "\033[2J\033[H";
    std::cout << "\033[97;1m                    DR. MARIO — VS BOT\033[0m\n\n";

    game.player.render_board("PLAYER", 2, true, game.player_attacks.size(), game.anim_frame);
    game.bot.render_board("  BOT", 38, false, game.bot_attacks.size(), game.anim_frame);

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
    game.drop_speed = drop_speed_int;

    // ---- start music ----
    if (access("queque.mp3", F_OK) == 0) {
        game.music_pid = fork();
        if (game.music_pid == 0) {
            setpgid(0, 0);  // create own process group for clean kill
            while (true) {
                pid_t p = fork();
                if (p == 0) {
                    execlp("afplay", "afplay", "queque.mp3", nullptr);
                    _exit(1);
                }
                wait(nullptr);  // wait for afplay to finish, then loop
            }
        }
    }

    // ---- init ----
    unsigned int seed = static_cast<unsigned>(std::time(nullptr));
    game.player.init(nv, seed);
    game.bot.init(nv, seed);

    int player_last_drop = 0;
    int player_last_gravity = 0;
    int bot_last_drop = 0;
    int bot_last_gravity = 0;
    int bot_last_move = 0;

    // ---- game loop ----
    while (!(game.player.game_over || game.player.game_won || game.bot.game_won || game.bot.game_over)) {
        // ====== GATHER INPUTS ======
        Move player_move = Move::NONE;
        if (game.player.phase == Phase::PLAYING) {
            player_move = get_player_move();
        } else {
            player_move = get_player_drain_input_check_quit();
        }
        if (player_move == Move::QUIT) break;

        Move bot_move = Move::NONE;
        if (game.bot.phase == Phase::PLAYING && ((game.ticks - bot_last_move) >= BOT_INPUT_TICK_RATE)) {
            bot_last_move = game.ticks;
            bot_move = get_bot_move(game.bot, game.bot_state);
        }

        // ====== APPLY MOVES ======
        if (game.player.phase == Phase::PLAYING) game.player.apply_move(player_move);
        if (game.bot.phase == Phase::PLAYING)     game.bot.apply_move(bot_move);

        // ====== PHASE PROCESSING ======
        process_phases(game.player, game.player_attacks, game.bot_attacks,
                       player_last_drop, player_last_gravity, true);
        process_phases(game.bot, game.bot_attacks, game.player_attacks,
                       bot_last_drop, bot_last_gravity, false);
        render();
        game.ticks++;
        std::this_thread::sleep_for(std::chrono::milliseconds(((int)std::ceilf(1000.0/game_fps))));
    }

    // stop music (kill entire process group)
    if (game.music_pid > 0) kill(-game.music_pid, SIGTERM);

    render();
    std::cout << "\nPress 'q' to exit...\n";
    while (poll_key() != 'q') {
        input_sleep();
    }
    return 0;
}
