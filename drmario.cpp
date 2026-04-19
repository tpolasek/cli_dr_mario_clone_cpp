/*
 * Dr. Mario Clone — Terminal Edition (2-Player vs Bot)
 * Compile: make
 * Run:     ./drmario [--bot <name>]
 *          ./drmario --bot1 <name> --bot2 <name>
 * Controls: A/D = Move, S = Drop, W = Rotate, Q = Quit
 */

#include "board.h"
#include "bot/bot_bfs.h"
#include "bot/bot_random.h"
#include "renderer.h"
#include "terminal_io.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

// ====================== GAME STATE ======================
static volatile sig_atomic_t quit_requested = 0;

static void sigint_handler(int) { quit_requested = 1; }

static int game_fps = GAME_FPS;

// ====================== HELPERS ======================

void new_piece_with_speed(PlayerBoard &board, float &drop_speed) {
  board.new_piece();
  drop_speed = std::fmax(5, drop_speed - 1.0 / 10.0); // 1/10 tick increment
}

// ====================== INPUT ======================

Move get_player_drain_input_check_quit() {
  while (true) {
    int ch = poll_key();
    if (ch == 0)
      return Move::NONE;
    if (ch == 'q' || ch == 'Q')
      return Move::QUIT;
  }
}

Move get_player_move() {
  Move move = Move::NONE;
  while (true) {
    int ch = poll_key();
    if (ch == 0)
      break;

    if (ch == 'q' || ch == 'Q') {
      return Move::QUIT;
    }

    switch (ch) {
    case 'a':
    case 'A':
    case KEY_LEFT:
      move = Move::LEFT;
      break;
    case 'd':
    case 'D':
    case KEY_RIGHT:
      move = Move::RIGHT;
      break;
    case 's':
    case 'S':
    case KEY_DOWN:
      move = Move::DROP;
      break;
    case 'w':
    case 'W':
    case KEY_UP:
      move = Move::ROTATE;
      break;
    }
  }
  return move;
}

void input_sleep() {
  std::this_thread::sleep_for(std::chrono::microseconds(100));
}

// ====================== PHASE TRANSITIONS ======================

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

// Unified phase processing for both sides.
// Returns true if game ended (game_won or game_over).
bool process_phases(PlayerBoard &board, std::queue<int> &my_attacks,
                    std::queue<int> &opponent_attacks, int &last_drop,
                    int &last_gravity, int ticks, float &drop_speed) {
  if (board.cleared_viruses >= board.total_viruses) {
    board.game_won = true;
    return true;
  }
  switch (board.phase) {
  case Phase::PLAYING: {
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
    if (board.gravity_step())
      return false;

    if (board.find_and_remove_matches() > 0)
      return false;

    board.flush_cascade(opponent_attacks);

    if (!my_attacks.empty()) {
      if (!board.receive_attacks(my_attacks)) {
        board.game_over = true;
        return true;
      }
      return false;
    }

    board.phase = Phase::PLAYING;
    new_piece_with_speed(board, drop_speed);
    last_drop = ticks;
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
  unsigned int seed = 0; // --seed <uint>    (0 means random)
  bool battle = false;   // true if --bot1 and --bot2 both set
};

static void print_usage(const char *prog) {
  std::cerr << "Usage:\n"
            << "  " << prog
            << " [--bot <name>]          Player vs Bot (interactive)\n"
            << "  " << prog
            << " --bot1 <name> --bot2 <name>  Bot vs Bot (headless)\n"
            << "\nOptions:\n"
            << "  --bot <name>   Select bot opponent for interactive mode\n"
            << "  --bot1 <name>  Bot 1 for battle mode\n"
            << "  --bot2 <name>  Bot 2 for battle mode\n"
            << "  --seed <uint>  Game seed for battle mode (0 = random)\n"
            << "  -h, --help     Show this help\n"
            << "\nAvailable bots:\n";
  for (const auto &entry : BotRegistry::instance().list()) {
    std::cerr << "  " << entry.name << " — " << entry.description << "\n";
  }
}

static CliArgs parse_args(int argc, char *argv[]) {
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
    } else if (arg == "--seed" && i + 1 < argc) {
      args.seed = static_cast<unsigned>(std::stoul(argv[++i]));
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

static int run_bot_battle(const CliArgs &args) {
  constexpr int BATTLE_VIRUSES = 40;
  constexpr float BATTLE_DROP_SPEED = 40;
  constexpr int NUM_TRIALS = 10;
  constexpr int FAIL_TO_FINISH_GAME_PENALTY_TICKS = 4000;
  constexpr int MIN_LOSS_TICK_THRESHOLD = 300; // ~5 seconds (60fps)

  auto bot1 = BotRegistry::instance().create(args.bot1);
  auto bot2 = BotRegistry::instance().create(args.bot2);
  if (!bot1 || !bot2) {
    std::cerr << "Failed to create bots\n";
    return 0;
  }

  int wins1 = 0, wins2 = 0;
  int bot1_ticks_loss_delta_sum = 0, bot2_ticks_loss_delta_sum = 0;
  // Use the binary's address space layout as additional entropy instead.
  unsigned int base_seed =
      args.seed != 0
          ? args.seed
          : static_cast<unsigned>(std::time(nullptr) ^
                                  reinterpret_cast<uintptr_t>(&wins1));

  for (int trial = 0; trial < NUM_TRIALS && !quit_requested; trial++) {
    bot1->reset();
    bot2->reset();

    PlayerBoard board1, board2;
    unsigned int seed =
        base_seed ^ (static_cast<unsigned>(trial) * 0x9e3779b9u);
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
    int first_winner = -1;
    int loser_ticks_delta = 0;

    const auto render_interval = std::chrono::milliseconds(1000 / RENDER_FPS);
    auto last_render_time = Clock::now();

    // This runs until both bots finish, even if one of them wins first.
    while (!(board1.game_over || board2.game_over ||
             (board1.game_won && board2.game_won))) {
      Move move1 = Move::NONE;
      if (!board1.game_won && board1.phase == Phase::PLAYING &&
          (ticks - last_move1 >= BOT_INPUT_TICK_RATE)) {
        last_move1 = ticks;
        move1 = bot1->get_move(board1);
      }

      Move move2 = Move::NONE;
      if (!board2.game_won && board2.phase == Phase::PLAYING &&
          (ticks - last_move2 >= BOT_INPUT_TICK_RATE)) {
        last_move2 = ticks;
        move2 = bot2->get_move(board2);
      }

      if (board1.phase == Phase::PLAYING)
        board1.apply_move(move1);
      if (board2.phase == Phase::PLAYING)
        board2.apply_move(move2);

      if (!board1.game_won) {
        process_phases(board1, attacks_to_1, attacks_to_2, last_drop1,
                       last_grav1, ticks, drop1);
      }
      if (!board2.game_won) {
        process_phases(board2, attacks_to_2, attacks_to_1, last_drop2,
                       last_grav2, ticks, drop2);
      }

      // Determine who won first.
      if (first_winner == -1) {
        if (board1.game_won && board2.game_won) {
          // tie
          first_winner = 0;
        } else {
          if (board1.game_won && !board2.game_won) {
            first_winner = 1;
          }
          if (board2.game_won && !board1.game_won) {
            first_winner = 2;
          }
        }
      } else {
        // We have a winner, count ticks for the loser
        loser_ticks_delta++;
      }

      auto now = Clock::now();
      if (now - last_render_time >= render_interval || board1.game_over ||
          board2.game_over || (board1.game_won && board2.game_won)) {
        render_game(board1, board2, attacks_to_1.size(), attacks_to_2.size(),
                    anim_frame);
        std::cout << "\n\033[" << (45) << ";1H\033[2K"
                  << "\033[97mTrial " << (trial + 1) << "/" << NUM_TRIALS
                  << "  \033[92m" << args.bot1 << ": " << wins1 << "  \033[91m"
                  << args.bot2 << ": " << wins2 << "\033[0m";
        std::cout.flush();
        last_render_time = now;
      }

      // std::this_thread::sleep_for(std::chrono::milliseconds(((int)std::ceil(1000.0/game_fps))));
      ticks++;
    }

    if (board2.game_over && !board1.game_over) {
      wins1++;
      bot2_ticks_loss_delta_sum +=
          loser_ticks_delta + FAIL_TO_FINISH_GAME_PENALTY_TICKS;
      continue;
    }
    if (board1.game_over && !board2.game_over) {
      wins2++;
      bot1_ticks_loss_delta_sum +=
          loser_ticks_delta + FAIL_TO_FINISH_GAME_PENALTY_TICKS;
      continue;
    }

    if (first_winner == 1) {
      if (loser_ticks_delta > MIN_LOSS_TICK_THRESHOLD) {
        wins1++;
      }
      bot2_ticks_loss_delta_sum += loser_ticks_delta;
      continue;
    }
    if (first_winner == 2) {
      if (loser_ticks_delta > MIN_LOSS_TICK_THRESHOLD) {
        wins2++;
      }
      bot1_ticks_loss_delta_sum += loser_ticks_delta;
      continue;
    }
  }

  // Final results
  std::cout << "\n\n  " << NUM_TRIALS << " trials complete\n"
            << "    \033[92m" << args.bot1 << ": " << wins1 << " wins\033[0m"
            << " (TickLossDelta/" << bot1_ticks_loss_delta_sum << ")" << "\n"
            << "    \033[91m" << args.bot2 << ": " << wins2 << " wins\033[0m"
            << " (TickLossDelta/" << bot2_ticks_loss_delta_sum << ")" << "\n\n";

  if (wins1 > wins2) {
    std::cout << "  \033[92;1m" << args.bot1 << " wins the series!\033[0m\n";
    return 1;
  }
  if (wins2 > wins1) {
    std::cout << "  \033[91;1m" << args.bot2 << " wins the series!\033[0m\n";
    return 2;
  }

  // Tie now check ticks

  if (bot1_ticks_loss_delta_sum < bot2_ticks_loss_delta_sum) {
    std::cout << "  \033[92;1m" << args.bot1 << " wins the series!\033[0m\n";
    return 1;
  }

  if (bot1_ticks_loss_delta_sum > bot2_ticks_loss_delta_sum) {
    std::cout << "  \033[92;1m" << args.bot2 << " wins the series!\033[0m\n";
    return 2;
  }

  // Tie x2
  std::cout << "  \033[93;1mIt's a tie!\033[0m\n";
  return 0;
}

// ====================== PLAYER VS BOT MODE ======================

static int run_player_vs_bot(const CliArgs &args) {
  signal(SIGINT, sigint_handler);
  enable_raw_mode();
  render_enter_alt_screen();

  // ---- menu ----
  auto select_option = [](std::initializer_list<std::pair<char, int>> options,
                          int &result) -> bool {
    result = 0;
    while (result == 0) {
      if (quit_requested)
        return false;
      int ch = poll_key();
      if (ch == 'q' || ch == 'Q')
        return false;
      for (auto &[key, val] : options)
        if (ch == key) {
          result = val;
          break;
        }
      if (result == 0)
        input_sleep();
    }
    return true;
  };

  render_title();

  render_mode_menu();

  int nv;
  int drop_speed_int;
  std::string bot_name;

  int mode = 0;
  while (mode == 0) {
    if (quit_requested) {
      render_clear_screen();
      return 0;
    }
    int ch = poll_key();
    if (ch == 'q' || ch == 'Q') {
      render_clear_screen();
      return 0;
    }
    if (ch == '1' || ch == '2' || ch == '3') {
      mode = ch - '0';
    }
    if (mode == 0)
      input_sleep();
  }

  if (mode == 1) {
    // Jane mode
    nv = 3;
    drop_speed_int = 60;
    bot_name = "kid";
  } else if (mode == 2) {
    // Dad mode
    nv = 40;
    drop_speed_int = 50;
    bot_name = "swift";
  } else {
    // Test mode
    nv = 1;
    drop_speed_int = 80;
    bot_name = "kid";
  }

  auto bot_bfs = BotRegistry::instance().create(bot_name);
  if (!bot_bfs) {
    std::cerr << "Failed to create bot: " << bot_name << "\n";
    return 1;
  }

  // ---- music helpers ----
  pid_t music_pid = 0;
  bool has_music = access("queque.mp3", F_OK) == 0;

  auto start_music = [&]() {
    if (!has_music || music_pid > 0)
      return;
    music_pid = fork();
    if (music_pid == 0) {
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
  };

  auto stop_music = [&]() {
    if (music_pid > 0) {
      kill(-music_pid, SIGTERM);
      music_pid = 0;
    }
  };

  // ---- continuous round loop ----
  int wins = 0;
  int losses = 0;
  int round_num = 0;

  while (!quit_requested) {
    round_num++;
    // +1 virus every 5 rounds (starting at round 6)
    int virus_count = nv + ((round_num - 1) / 5);

    start_music();

    bot_bfs->reset();

    PlayerBoard player, bot_board;
    unsigned int seed = static_cast<unsigned>(std::time(nullptr)) ^
                        static_cast<unsigned>(round_num);
    player.init(virus_count, seed);
    bot_board.init(virus_count, seed);

    std::queue<int> player_attacks;
    std::queue<int> bot_attacks;

    float drop_speed = drop_speed_int;
    int player_last_drop = 0;
    int player_last_gravity = 0;
    int bot_last_drop = 0;
    int bot_last_gravity = 0;
    int bot_last_move = 0;
    int ticks = 0;
    int anim_frame = 0;

    // ---- round game loop ----
    const auto render_interval = std::chrono::milliseconds(1000 / RENDER_FPS);
    auto last_render_time = Clock::now();

    bool round_over = false;
    bool player_won = false;

    while (!quit_requested) {
      // Check end conditions
      if (player.game_over || bot_board.game_won) {
        // Player lost (or bot cleared all viruses first)
        round_over = true;
        player_won = false;
        break;
      }
      if (bot_board.game_over || player.game_won) {
        // Player won (bot topped out or player cleared all viruses)
        round_over = true;
        player_won = true;
        break;
      }

      Move player_move = Move::NONE;
      if (player.phase == Phase::PLAYING) {
        player_move = get_player_move();
      } else {
        player_move = get_player_drain_input_check_quit();
      }
      if (player_move == Move::QUIT) {
        quit_requested = 1;
        break;
      }

      Move bot_move = Move::NONE;
      if (bot_board.phase == Phase::PLAYING &&
          ((ticks - bot_last_move) >= BOT_INPUT_TICK_RATE)) {
        bot_last_move = ticks;
        bot_move = bot_bfs->get_move(bot_board);
      }

      if (player.phase == Phase::PLAYING)
        player.apply_move(player_move);
      if (bot_board.phase == Phase::PLAYING)
        bot_board.apply_move(bot_move);

      process_phases(player, player_attacks, bot_attacks, player_last_drop,
                     player_last_gravity, ticks, drop_speed);
      process_phases(bot_board, bot_attacks, player_attacks, bot_last_drop,
                     bot_last_gravity, ticks, drop_speed);

      // Re-check after processing phases (cascade wins can happen here)
      if (player.game_won || bot_board.game_over) {
        round_over = true;
        player_won = true;
        break;
      }
      if (player.game_over || bot_board.game_won) {
        round_over = true;
        player_won = false;
        break;
      }

      auto now = Clock::now();
      if (now - last_render_time >= render_interval) {
        render_game(player, bot_board, player_attacks.size(),
                    bot_attacks.size(), anim_frame, wins, losses, round_num);
        last_render_time = now;
      }

      ticks++;
      std::this_thread::sleep_for(
          std::chrono::milliseconds(((int)std::ceil(1000.0 / game_fps))));
    }

    if (quit_requested)
      break;

    if (!round_over)
      break; // safety exit

    // Update win/loss tracking
    if (player_won) {
      wins++;
    } else {
      losses++;
    }

    // Final render of the game state
    render_game(player, bot_board, player_attacks.size(), bot_attacks.size(),
                anim_frame, wins, losses, round_num);
    std::cout.flush();

    stop_music();

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Show round-end screen
    render_round_end(player_won, wins, losses, round_num, round_num);

    // Sleep 2 seconds, draining any queued inputs so they don't
    // accidentally skip the round-end screen
    std::this_thread::sleep_for(std::chrono::seconds(2));
    while (poll_key() != 0)
      ; // drain

    // Now wait for a real key press to continue (Q quits)
    while (!quit_requested) {
      int ch = poll_key();
      if (ch != 0) {
        if (ch == 'q' || ch == 'Q') {
          quit_requested = 1;
        }
        break;
      }
      input_sleep();
    }
  }

  stop_music();

  render_clear_screen();

  // Final stats
  int total = wins + losses;
  if (total > 0) {
    int pct = (wins * 100) / total;
    std::cout << "\n\n  \033[97;1mFinal Record: \033[92m" << wins
              << " wins\033[0m"
              << " \033[91m" << losses << " losses\033[0m"
              << " \033[93m(" << pct << "% win rate)\033[0m"
              << " over " << round_num << " rounds\n\n";
  }

  return 0;
}

// ====================== MAIN ======================

int main(int argc, char *argv[]) {
  std::srand(static_cast<unsigned>(std::time(nullptr)));

  CliArgs args = parse_args(argc, argv);

  if (args.battle) {
    return run_bot_battle(args);
  }
  return run_player_vs_bot(args);
}
