/*
 * Dr. Mario Clone — Terminal Edition (2-Player vs Bot)
 * Compile: make
 * Run:     ./drmario [--bot <name>]
 *          ./drmario --bot1 <name> --bot2 <name>
 * Controls: A/D = Move, S = Drop, W = Rotate, Q = Quit
 */

#include "board.h"
#include "bot/bot_ai.h"
#include "bot/bot_random.h"
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
    std::unique_ptr<Bot> bot_ai;       // polymorphic bot (player-vs-bot mode)
    std::queue<int> player_attacks;    // attacks TO player (from bot)
    std::queue<int> bot_attacks;       // attacks TO bot (from player)
    float drop_speed = 24;
    int ticks = 0;
    int anim_frame = 0;
    pid_t music_pid = 0;
};

// ====================== HELPERS ======================

void new_piece_with_speed(PlayerBoard& board, float& drop_speed) {
    board.new_piece();
    drop_speed = std::fmax(5, drop_speed - 1.0/6.0); // 1/6 tick increment
}

// ====================== INPUT ======================

Move get_player_drain_input_check_quit() {
    while (true) {
        int ch = poll_key();
        if (ch == 0) return Move::NONE;
        if (ch == 'q' || ch == 'Q') return Move::QUIT;
    }
}

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
    std::this_thread::sleep_for(std::chrono::microseconds(100));
}

// ====================== PHASE TRANSITIONS ======================

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

// Unified phase processing for both sides.
// Returns true if game ended (game_won or game_over).
bool process_phases(PlayerBoard& board, std::queue<int>& my_attacks, std::queue<int>& opponent_attacks,
                    int& last_drop, int& last_gravity, int ticks, float& drop_speed) {
    switch (board.phase) {
    case Phase::PLAYING: {
        if (board.cleared_viruses >= board.total_viruses) {
            board.game_won = true;
            return true;
        }

        if (ticks - last_drop <= std::ceil(drop_speed))
            return false;

        last_drop = ticks;
        Capsule t = board.cap;
        t.r++;
        if (board.fits(t)) {
            board.cap = t;
            return false;
        }

        board.stamp(board.cap);

        if (board.find_and_remove_matches() > 0) {
            board.phase = Phase::GRAVITY;
            last_gravity = ticks;
            return false;
        }

        if (!my_attacks.empty()) {
            if (!board.receive_attacks(my_attacks)) {
                board.game_over = true;
                return true;
            }
            board.phase = Phase::GRAVITY;
            last_gravity = ticks;
            return false;
        }

        new_piece_with_speed(board, drop_speed);
        return false;
    }

    case Phase::GRAVITY: {
        if (ticks - last_gravity < GRAVITY_TICK_RATE)
            return false;

        last_gravity = ticks;
        if (board.gravity_step()) return false;

        if (board.find_and_remove_matches() > 0) return false;

        board.flush_cascade(opponent_attacks);

        if (board.cleared_viruses >= board.total_viruses) {
            board.game_won = true;
        } else {
            board.phase = Phase::PLAYING;
            new_piece_with_speed(board, drop_speed);
            last_drop = ticks;
        }
        return false;
    }
    }
    return false;
}

// ====================== CLI ARGUMENTS ======================

struct CliArgs {
    std::string bot;       // --bot <name>     (player-vs-bot)
    std::string bot1;      // --bot1 <name>    (bot-vs-bot)
    std::string bot2;      // --bot2 <name>    (bot-vs-bot)
    bool battle = false;   // true if --bot1 and --bot2 both set
};

static void print_usage(const char* prog) {
    std::cerr << "Usage:\n"
              << "  " << prog << " [--bot <name>]          Player vs Bot (interactive)\n"
              << "  " << prog << " --bot1 <name> --bot2 <name>  Bot vs Bot (headless)\n"
              << "\nOptions:\n"
              << "  --bot <name>   Select bot opponent for interactive mode\n"
              << "  --bot1 <name>  Bot 1 for battle mode\n"
              << "  --bot2 <name>  Bot 2 for battle mode\n"
              << "  -h, --help     Show this help\n"
              << "\nAvailable bots:\n";
    for (const auto& entry : BotRegistry::instance().list()) {
        std::cerr << "  " << entry.name << " — " << entry.description << "\n";
    }
}

static CliArgs parse_args(int argc, char* argv[]) {
    CliArgs args;
    for (int i = 1; i < argc; i++) {
        std::string arg(argv[i]);

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            std::exit(0);
        }
        if (arg == "--bot" && i + 1 < argc) {
            args.bot = argv[++i];
        } else if (arg == "--bot1" && i + 1 < argc) {
            args.bot1 = argv[++i];
        } else if (arg == "--bot2" && i + 1 < argc) {
            args.bot2 = argv[++i];
        }
    }

    if (!args.bot1.empty() || !args.bot2.empty()) {
        // Battle mode requires both bots
        if (args.bot1.empty()) {
            std::cerr << "Error: --bot1 is required when --bot2 is specified\n";
            print_usage(argv[0]);
            std::exit(1);
        }
        if (args.bot2.empty()) {
            std::cerr << "Error: --bot2 is required when --bot1 is specified\n";
            print_usage(argv[0]);
            std::exit(1);
        }
        if (!BotRegistry::instance().has(args.bot1)) {
            std::cerr << "Unknown bot: " << args.bot1 << "\n";
            print_usage(argv[0]);
            std::exit(1);
        }
        if (!BotRegistry::instance().has(args.bot2)) {
            std::cerr << "Unknown bot: " << args.bot2 << "\n";
            print_usage(argv[0]);
            std::exit(1);
        }
        args.battle = true;
    } else if (!args.bot.empty()) {
        if (!BotRegistry::instance().has(args.bot)) {
            std::cerr << "Unknown bot: " << args.bot << "\n";
            print_usage(argv[0]);
            std::exit(1);
        }
    }

    return args;
}

// ====================== BOT VS BOT MODE ======================

static int run_bot_battle(const CliArgs& args) {
    constexpr int BATTLE_VIRUSES      = 5;
    constexpr float BATTLE_DROP_SPEED = 40;
    constexpr int NUM_TRIALS          = 1;

    auto bot1 = BotRegistry::instance().create(args.bot1);
    auto bot2 = BotRegistry::instance().create(args.bot2);
    if (!bot1 || !bot2) {
        std::cerr << "Failed to create bots\n";
        return 0;
    }

    int wins1 = 0, wins2 = 0;
    unsigned int base_seed = static_cast<unsigned>(std::time(nullptr));

    for (int trial = 0; trial < NUM_TRIALS && !quit_requested; trial++) {
        bot1->reset();
        bot2->reset();

        PlayerBoard board1, board2;
        unsigned int seed = base_seed + trial;
        board1.init(BATTLE_VIRUSES, seed);
        board2.init(BATTLE_VIRUSES, seed);

        std::queue<int> attacks_to_1;
        std::queue<int> attacks_to_2;

        float drop1 = BATTLE_DROP_SPEED;
        float drop2 = BATTLE_DROP_SPEED;
        int last_drop1 = 0, last_drop2 = 0;
        int last_grav1 = 0, last_grav2 = 0;
        int last_move1 = 0, last_move2 = 0;
        int ticks = 0;
        int anim_frame = 0;

        const auto render_interval = std::chrono::milliseconds(1000 / RENDER_FPS);
        auto last_render_time = Clock::now();

        while (!(board1.game_over || board1.game_won || board2.game_won || board2.game_over)) {
            Move move1 = Move::NONE;
            if (board1.phase == Phase::PLAYING && (ticks - last_move1 >= BOT_INPUT_TICK_RATE)) {
                last_move1 = ticks;
                move1 = bot1->get_move(board1);
            }

            Move move2 = Move::NONE;
            if (board2.phase == Phase::PLAYING && (ticks - last_move2 >= BOT_INPUT_TICK_RATE)) {
                last_move2 = ticks;
                move2 = bot2->get_move(board2);
            }

            if (board1.phase == Phase::PLAYING) board1.apply_move(move1);
            if (board2.phase == Phase::PLAYING) board2.apply_move(move2);

            process_phases(board1, attacks_to_1, attacks_to_2,
                           last_drop1, last_grav1, ticks, drop1);
            process_phases(board2, attacks_to_2, attacks_to_1,
                           last_drop2, last_grav2, ticks, drop2);

            auto now = Clock::now();
            if (now - last_render_time >= render_interval) {
                render_game(board1, board2, attacks_to_1.size(), attacks_to_2.size(), anim_frame);
                std::cout << "\n\033[" << (45) << ";1H\033[2K"
                          << "\033[97mTrial " << (trial + 1) << "/" << NUM_TRIALS
                          << "  \033[92m" << args.bot1 << ": " << wins1
                          << "  \033[91m" << args.bot2 << ": " << wins2
                          << "\033[0m";
                std::cout.flush();
                last_render_time = now;
            }

            ticks++;
        }

        if (board1.game_won || board2.game_over) wins1++;
        else if (board2.game_won || board1.game_over) wins2++;
    }

    // Final results
    std::cout << "\n\n  " << NUM_TRIALS << " trials complete\n"
              << "    \033[92m" << args.bot1 << ": " << wins1 << " wins\033[0m\n"
              << "    \033[91m" << args.bot2 << ": " << wins2 << " wins\033[0m\n\n";

    if (wins1 > wins2) {
        std::cout << "  \033[92;1m" << args.bot1 << " wins the series!\033[0m\n";
        return 1;
    }
    if (wins2 > wins1) {
        std::cout << "  \033[91;1m" << args.bot2 << " wins the series!\033[0m\n";
        return 2;
    }
    std::cout << "  \033[93;1mIt's a tie!\033[0m\n";
    return 0;
}

// ====================== PLAYER VS BOT MODE ======================

static int run_player_vs_bot(const CliArgs& args) {
    Game game;

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
    std::string bot_name = args.bot;

    if (bot_name.empty()) {
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
            setpgid(0, 0);
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
                wait(nullptr);
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

        if (game.player.phase == Phase::PLAYING) game.player.apply_move(player_move);
        if (game.bot.phase == Phase::PLAYING)     game.bot.apply_move(bot_move);

        process_phases(game.player, game.player_attacks, game.bot_attacks,
                       player_last_drop, player_last_gravity, game.ticks, game.drop_speed);
        process_phases(game.bot, game.bot_attacks, game.player_attacks,
                       bot_last_drop, bot_last_gravity, game.ticks, game.drop_speed);

        auto now = Clock::now();
        if (now - last_render_time >= render_interval) {
            render_game(game.player, game.bot, game.player_attacks.size(), game.bot_attacks.size(), game.anim_frame);
            last_render_time = now;
        }

        game.ticks++;
        std::this_thread::sleep_for(std::chrono::milliseconds(((int)std::ceilf(1000.0/game_fps))));
    }

    if (game.music_pid > 0) kill(-game.music_pid, SIGTERM);

    render_game(game.player, game.bot, game.player_attacks.size(), game.bot_attacks.size(), game.anim_frame);
    std::cout << "\nPress 'q' to exit...\n";
    while (poll_key() != 'q' && !quit_requested) {
        input_sleep();
    }
    return 0;
}

// ====================== MAIN ======================

int main(int argc, char* argv[]) {
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    CliArgs args = parse_args(argc, argv);

    if (args.battle) {
        return run_bot_battle(args);
    }
    return run_player_vs_bot(args);
}
