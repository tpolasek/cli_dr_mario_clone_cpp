/*
 * Dr. Mario Clone — Terminal Edition (2-Player vs Bot)
 * Compile: make
 * Run:     ./drmario [--bot <name>]
 * Controls: A/D = Move, S = Drop, W = Rotate, Q = Quit
 */

#include "board.h"
#include "bot_ai.h"         // pulls in bot_registry.h
#include "bot_random.h"
#include "terminal_io.h"
#include "renderer.h"

#include <cmath>
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>

// ====================== GAME STATE ======================
static volatile sig_atomic_t quit_requested = 0;

static void sigint_handler(int) {
    quit_requested = 1;
}

static int game_fps = GAME_FPS;

struct Game {
    PlayerBoard player;
    PlayerBoard bot;
    std::unique_ptr<Bot> bot_ai;       // polymorphic bot
    std::queue<int> player_attacks;    // attacks TO player (from bot)
    std::queue<int> bot_attacks;       // attacks TO bot (from player)
    float drop_speed = 24;
    int ticks = 0;
    int anim_frame = 0;
    pid_t music_pid = 0;
};

static Game game;
// ====================== HELPERS ======================

void new_piece_with_speed(PlayerBoard& board, float& drop_speed) {
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
    std::this_thread::sleep_for(std::chrono::microseconds(100));
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
        new_piece_with_speed(board, game.drop_speed);
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
            new_piece_with_speed(board, game.drop_speed);
            last_drop = game.ticks;
        }
        return false;
    }
    }
    return false;
}

// ====================== CLI ARGUMENTS ======================

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog << " [--bot <name>]\n"
              << "\nAvailable bots:\n";
    for (const auto& entry : BotRegistry::instance().list()) {
        std::cerr << "  " << entry.name << " — " << entry.description << "\n";
    }
}

// Parse --bot <name> from argv. Returns the bot name or empty string if not specified.
static std::string parse_bot_arg(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--bot" && i + 1 < argc) {
            return argv[i + 1];
        }
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        }
    }
    return "";
}

// ====================== MAIN ======================

int main(int argc, char* argv[]) {
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    // ---- parse CLI before entering raw mode ----
    std::string cli_bot = parse_bot_arg(argc, argv);
    if (!cli_bot.empty() && !BotRegistry::instance().has(cli_bot)) {
        std::cerr << "Unknown bot: " << cli_bot << "\n";
        print_usage(argv[0]);
        return 1;
    }

    signal(SIGINT, sigint_handler);

    enable_raw_mode();
    render_enter_alt_screen();

    // ---- menu ----
    auto select_option = [](std::initializer_list<std::pair<char, int>> options, int& result) -> bool {
        result = 0;
        while (result == 0) {
            if (quit_requested) return false;
            int ch = poll_key();
            if (ch == 'q' || ch == 'Q') return false;
            for (auto& [key, val] : options)
                if (ch == key) { result = val; break; }
            if (result == 0)
                input_sleep();
        }
        return true;
    };

    render_title();

    int nv;
    render_virus_menu();
    if (!select_option({{'1', 5}, {'2', 10}, {'3', 20}, {'4', 30}}, nv)) {
        render_clear_screen();
        return 0;
    }

    render_speed_menu();

    int drop_speed_int;
    if (!select_option({{'1', 40}, {'2', 24}, {'3', 12}}, drop_speed_int)) {
        render_clear_screen();
        return 0;
    }
    game.drop_speed = drop_speed_int;

    // ---- bot selection ----
    std::string bot_name = cli_bot;  // prefer CLI override

    if (bot_name.empty()) {
        // Interactive bot menu
        const auto& bots = BotRegistry::instance().list();
        render_bot_menu(bots);

        int bot_index = -1;
        while (bot_index < 0) {
            if (quit_requested) {
                render_clear_screen();
                return 0;
            }
            int ch = poll_key();
            if (ch == 'q' || ch == 'Q') {
                render_clear_screen();
                return 0;
            }
            if (ch >= '1' && ch <= static_cast<char>('0' + (int)bots.size())) {
                bot_index = ch - '1';
            }
            if (bot_index < 0)
                input_sleep();
        }
        bot_name = bots[bot_index].name;
    }

    game.bot_ai = BotRegistry::instance().create(bot_name);
    if (!game.bot_ai) {
        std::cerr << "Failed to create bot: " << bot_name << "\n";
        return 1;
    }

    // ---- start music ----
    if (access("queque.mp3", F_OK) == 0) {
        game.music_pid = fork();
        if (game.music_pid == 0) {
            setpgid(0, 0);  // create own process group for clean kill
            while (true) {
                pid_t p = fork();
                if (p == 0) {
                    int null_fd = open("/dev/null", O_WRONLY);
                    if (null_fd >= 0) {
                        dup2(null_fd, STDOUT_FILENO);
                        dup2(null_fd, STDERR_FILENO);
                        close(null_fd);
                    }
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
    const auto render_interval = std::chrono::milliseconds(1000 / RENDER_FPS);
    auto last_render_time = Clock::now();

    while (!(game.player.game_over || game.player.game_won || game.bot.game_won || game.bot.game_over || quit_requested)) {
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
            bot_move = game.bot_ai->get_move(game.bot);
        }

        // ====== APPLY MOVES ======
        if (game.player.phase == Phase::PLAYING) game.player.apply_move(player_move);
        if (game.bot.phase == Phase::PLAYING)     game.bot.apply_move(bot_move);

        // ====== PHASE PROCESSING ======
        process_phases(game.player, game.player_attacks, game.bot_attacks,
                       player_last_drop, player_last_gravity, true);
        process_phases(game.bot, game.bot_attacks, game.player_attacks,
                       bot_last_drop, bot_last_gravity, false);

        // ====== RENDER ======
        auto now = Clock::now();
        if (now - last_render_time >= render_interval) {
            render_game(game.player, game.bot, game.player_attacks.size(), game.bot_attacks.size(), game.anim_frame);
            last_render_time = now;
        }

        game.ticks++;

        std::this_thread::sleep_for(std::chrono::milliseconds(((int)std::ceilf(1000.0/game_fps))));
    }

    // stop music (kill entire process group)
    if (game.music_pid > 0) kill(-game.music_pid, SIGTERM);

    render_game(game.player, game.bot, game.player_attacks.size(), game.bot_attacks.size(), game.anim_frame);
    std::cout << "\nPress 'q' to exit...\n";
    while (poll_key() != 'q' && !quit_requested) {
        input_sleep();
    }
    return 0;
}
