/*
 * Dr. Mario Clone — Raylib Edition (2-Player vs Bot)
 * Compile: make
 * Run:     ./drmario
 *          ./drmario --bot1 <name> --bot2 <name>
 * Controls: Arrow keys = Move/Drop/Rotate, ESC = Quit
 */

#include "raylib.h"

// Undef raylib color macros that conflict with our game color constants
#undef RED
#undef YELLOW
#undef BLUE

#include "board.h"
#include "bot/bot_registry.h"
#include "game.h"
#include "raylib_renderer.h"
#include "sound.h"

#include <cstdlib>
#include <ctime>
#include <iostream>
#include <queue>
#include <string>
#include <vector>

// ====================== CONSTANTS ======================

static constexpr int SCREEN_W = 1920;
static constexpr int SCREEN_H = 1080;
static constexpr int TARGET_FPS = 60;

static constexpr int NUM_BATTLE_TRIALS = 10;
static constexpr int BATTLE_VIRUSES = 40;
static constexpr float BATTLE_DROP_SPEED = 40.0f;
static constexpr int BATTLE_TICKS_PER_FRAME = 10;
static constexpr int FAIL_TO_FINISH_PENALTY_TICKS = 4000;
static constexpr int MIN_LOSS_TICK_THRESHOLD = 300;

struct ModeConfig {
  int virus_count;
  int drop_speed;
  const char *bot_name;
};

static const ModeConfig MODES[] = {
    {3, 60, "kid"},    // Jane Mode
    {40, 50, "swift"}, // Dad Mode
    {1, 80, "kid"},    // Test Mode
};

// ====================== SCENES ======================

enum class Scene {
  TITLE,
  GAME,
  ROUND_END,
  BOT_BATTLE,
  BOT_BATTLE_RESULT,
  QUIT
};

// ====================== INPUT STATE ======================

struct InputState {
  int left_counter = 0;
  int right_counter = 0;
  int down_counter = 0;

  static constexpr int REPEAT_DELAY = 15; // frames before repeat starts
  static constexpr int REPEAT_RATE = 3;   // frames between repeats
};

static Move get_player_move(InputState &input) {
  // Rotate: no auto-repeat
  if (IsKeyPressed(KEY_UP))
    return Move::ROTATE;

  Move result = Move::NONE;

  auto check = [&](int key, int &counter, Move m) {
    if (IsKeyPressed(key)) {
      counter = 0;
      result = m;
      return;
    }
    if (IsKeyDown(key)) {
      counter++;
      if (counter > InputState::REPEAT_DELAY &&
          (counter - InputState::REPEAT_DELAY) % InputState::REPEAT_RATE ==
              0) {
        result = m;
      }
    } else {
      counter = 0;
    }
  };

  check(KEY_LEFT, input.left_counter, Move::LEFT);
  check(KEY_RIGHT, input.right_counter, Move::RIGHT);
  check(KEY_DOWN, input.down_counter, Move::DROP);

  return result;
}

// ====================== CLI ARGS ======================

struct CliArgs {
  std::string bot1;
  std::string bot2;
  unsigned int seed = 0;
  bool battle = false;
};

static void print_usage(const char *prog) {
  std::cerr << "Usage:\n"
            << "  " << prog
            << "                          Player vs Bot (GUI)\n"
            << "  " << prog
            << " --bot1 X --bot2 Y        Bot vs Bot (GUI)\n"
            << "\nOptions:\n"
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
    if (arg == "--bot1" && i + 1 < argc) {
      args.bot1 = argv[++i];
    } else if (arg == "--bot2" && i + 1 < argc) {
      args.bot2 = argv[++i];
    } else if (arg == "--seed" && i + 1 < argc) {
      args.seed = static_cast<unsigned>(std::stoul(argv[++i]));
    }
  }

  if (!args.bot1.empty() || !args.bot2.empty()) {
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
      std::exit(1);
    }
    if (!BotRegistry::instance().has(args.bot2)) {
      std::cerr << "Unknown bot: " << args.bot2 << "\n";
      std::exit(1);
    }
    args.battle = true;
  }

  return args;
}

// ====================== GAME STATE ======================

struct GameState {
  Scene scene = Scene::TITLE;

  // TITLE
  int title_selected = 0;
  std::vector<DecorVirus> decor_viruses;

  // GAME
  PlayerBoard player;
  PlayerBoard bot_board;
  std::unique_ptr<Bot> bot;
  std::queue<int> player_attacks;
  std::queue<int> bot_attacks;
  InputState input;
  float drop_speed = 60.0f;
  int player_last_drop = 0;
  int player_last_gravity = 0;
  int bot_last_drop = 0;
  int bot_last_gravity = 0;
  int bot_last_move = 0;
  int ticks = 0;
  int anim_frame = 0;
  int wins = 0;
  int losses = 0;
  int round_num = 0;

  // Mode settings (set from title selection)
  int virus_count = 3;
  int base_drop_speed = 60;
  std::string bot_name = "kid";

  // ROUND_END
  bool player_won = false;

  // BOT_BATTLE
  std::string battle_bot1_name;
  std::string battle_bot2_name;
  std::unique_ptr<Bot> battle_bot1;
  std::unique_ptr<Bot> battle_bot2;
  PlayerBoard battle_board1;
  PlayerBoard battle_board2;
  std::queue<int> attacks_to_1;
  std::queue<int> attacks_to_2;
  float battle_drop1 = BATTLE_DROP_SPEED;
  float battle_drop2 = BATTLE_DROP_SPEED;
  int battle_last_drop1 = 0;
  int battle_last_drop2 = 0;
  int battle_last_grav1 = 0;
  int battle_last_grav2 = 0;
  int battle_last_move1 = 0;
  int battle_last_move2 = 0;
  int battle_ticks = 0;
  int battle_trial = 0;
  int battle_wins1 = 0;
  int battle_wins2 = 0;
  int battle_first_winner = -1;
  int battle_loser_delta = 0;
  unsigned int battle_base_seed = 0;

  // BOT_BATTLE_RESULT
  int result_wins1 = 0;
  int result_wins2 = 0;
};

// ====================== INIT HELPERS ======================

static void init_title(GameState &state) {
  state.scene = Scene::TITLE;
  state.title_selected = 0;
  state.decor_viruses =
      generate_decor_viruses(25, SCREEN_W, SCREEN_H);
}

static void init_round(GameState &state) {
  state.round_num++;
  int virus_count = state.virus_count + ((state.round_num - 1) / 5);

  state.bot->reset();

  unsigned int seed = static_cast<unsigned>(std::time(nullptr)) ^
                      static_cast<unsigned>(state.round_num);
  state.player.init(virus_count, seed);
  state.bot_board.init(virus_count, seed);

  // Clear attack queues
  while (!state.player_attacks.empty())
    state.player_attacks.pop();
  while (!state.bot_attacks.empty())
    state.bot_attacks.pop();

  state.drop_speed = (float)state.base_drop_speed;
  state.player_last_drop = 0;
  state.player_last_gravity = 0;
  state.bot_last_drop = 0;
  state.bot_last_gravity = 0;
  state.bot_last_move = 0;
  state.ticks = 0;
  state.anim_frame = 0;
  state.input = InputState();
}

static void apply_mode(GameState &state, int mode_index) {
  const ModeConfig &cfg = MODES[mode_index];
  state.virus_count = cfg.virus_count;
  state.base_drop_speed = cfg.drop_speed;
  state.bot_name = cfg.bot_name;

  state.bot = BotRegistry::instance().create(state.bot_name);
  state.wins = 0;
  state.losses = 0;
  state.round_num = 0;
}

static void init_bot_battle_trial(GameState &state) {
  state.battle_bot1->reset();
  state.battle_bot2->reset();

  unsigned int seed = state.battle_base_seed ^
                      (static_cast<unsigned>(state.battle_trial) * 0x9e3779b9u);
  state.battle_board1.init(BATTLE_VIRUSES, seed);
  state.battle_board2.init(BATTLE_VIRUSES, seed);

  while (!state.attacks_to_1.empty())
    state.attacks_to_1.pop();
  while (!state.attacks_to_2.empty())
    state.attacks_to_2.pop();

  state.battle_drop1 = BATTLE_DROP_SPEED;
  state.battle_drop2 = BATTLE_DROP_SPEED;
  state.battle_last_drop1 = 0;
  state.battle_last_drop2 = 0;
  state.battle_last_grav1 = 0;
  state.battle_last_grav2 = 0;
  state.battle_last_move1 = 0;
  state.battle_last_move2 = 0;
  state.battle_ticks = 0;
  state.battle_first_winner = -1;
  state.battle_loser_delta = 0;
}

static void score_bot_battle_trial(GameState &state) {
  auto &b1 = state.battle_board1;
  auto &b2 = state.battle_board2;

  // Board2 topped out, board1 didn't
  if (b2.game_over && !b1.game_over) {
    state.battle_wins1++;
    return;
  }
  // Board1 topped out, board2 didn't
  if (b1.game_over && !b2.game_over) {
    state.battle_wins2++;
    return;
  }

  // Both finished — use first_winner
  if (state.battle_first_winner == 1) {
    if (state.battle_loser_delta > MIN_LOSS_TICK_THRESHOLD) {
      state.battle_wins1++;
    }
  } else if (state.battle_first_winner == 2) {
    if (state.battle_loser_delta > MIN_LOSS_TICK_THRESHOLD) {
      state.battle_wins2++;
    }
  }
  // Tie (first_winner == 0) → no winner for this trial
}

static void init_bot_battle(GameState &state, const CliArgs &args) {
  state.battle_bot1_name = args.bot1;
  state.battle_bot2_name = args.bot2;
  state.battle_bot1 = BotRegistry::instance().create(args.bot1);
  state.battle_bot2 = BotRegistry::instance().create(args.bot2);
  state.battle_wins1 = 0;
  state.battle_wins2 = 0;
  state.battle_trial = 0;
  state.battle_base_seed =
      args.seed != 0
          ? args.seed
          : static_cast<unsigned>(std::time(nullptr) ^
                                  reinterpret_cast<uintptr_t>(&state));
  init_bot_battle_trial(state);
  state.scene = Scene::BOT_BATTLE;
}

// ====================== SCENE UPDATES ======================

static void update_title(GameState &state) {
  int key = GetKeyPressed();
  while (key > 0) {
    if (key == KEY_UP) {
      state.title_selected =
          (state.title_selected + 2) % 3; // wrap backwards
    } else if (key == KEY_DOWN) {
      state.title_selected = (state.title_selected + 1) % 3;
    } else if (key == KEY_ESCAPE) {
      state.scene = Scene::QUIT;
      return;
    } else {
      // Any other key confirms selection
      apply_mode(state, state.title_selected);
      init_round(state);
      state.scene = Scene::GAME;
      return;
    }
    key = GetKeyPressed();
  }
}

static void update_game(GameState &state) {
  if (IsKeyPressed(KEY_ESCAPE)) {
    state.scene = Scene::QUIT;
    return;
  }

  // Player input
  Move player_move = Move::NONE;
  if (state.player.phase == Phase::PLAYING) {
    player_move = get_player_move(state.input);
  }

  // Bot input
  Move bot_move = Move::NONE;
  if (state.bot_board.phase == Phase::PLAYING &&
      (state.ticks - state.bot_last_move >= BOT_INPUT_TICK_RATE)) {
    state.bot_last_move = state.ticks;
    bot_move = state.bot->get_move(state.bot_board);
  }

  // Apply moves
  if (state.player.phase == Phase::PLAYING)
    state.player.apply_move(player_move);
  if (state.bot_board.phase == Phase::PLAYING)
    state.bot_board.apply_move(bot_move);

  // Process phases
  process_phases(state.player, state.player_attacks, state.bot_attacks,
                 state.player_last_drop, state.player_last_gravity,
                 state.ticks, state.drop_speed);
  process_phases(state.bot_board, state.bot_attacks, state.player_attacks,
                 state.bot_last_drop, state.bot_last_gravity, state.ticks,
                 state.drop_speed);

  state.ticks++;
  state.anim_frame++;

  // Check end conditions
  if (state.player.game_won || state.bot_board.game_over) {
    state.player_won = true;
    state.wins++;
    state.scene = Scene::ROUND_END;
    return;
  }
  if (state.player.game_over || state.bot_board.game_won) {
    state.player_won = false;
    state.losses++;
    state.scene = Scene::ROUND_END;
    return;
  }
}

static void update_round_end(GameState &state) {
  int key = GetKeyPressed();
  while (key > 0) {
    if (key == KEY_ESCAPE || key == KEY_Q) {
      state.scene = Scene::QUIT;
      return;
    }
    // Check if match is over (first to MATCH_WINS)
    if (state.wins >= MATCH_WINS || state.losses >= MATCH_WINS) {
      init_title(state);
      return;
    }
    // Any other key → next round
    init_round(state);
    state.scene = Scene::GAME;
    return;
  }
}

static void update_bot_battle(GameState &state) {
  if (IsKeyPressed(KEY_ESCAPE)) {
    state.scene = Scene::QUIT;
    return;
  }

  // Process multiple ticks per frame for faster playback
  for (int i = 0; i < BATTLE_TICKS_PER_FRAME; i++) {
    auto &b1 = state.battle_board1;
    auto &b2 = state.battle_board2;

    // Check if trial is over
    if (b1.game_over || b2.game_over ||
        (b1.game_won && b2.game_won)) {
      // Score this trial
      score_bot_battle_trial(state);

      state.battle_trial++;
      if (state.battle_trial >= NUM_BATTLE_TRIALS) {
        state.result_wins1 = state.battle_wins1;
        state.result_wins2 = state.battle_wins2;
        state.scene = Scene::BOT_BATTLE_RESULT;
        return;
      }

      // Next trial
      init_bot_battle_trial(state);
      return; // restart loop next frame
    }

    // Bot moves
    Move move1 = Move::NONE;
    if (!b1.game_won && b1.phase == Phase::PLAYING &&
        (state.battle_ticks - state.battle_last_move1 >=
         BOT_INPUT_TICK_RATE)) {
      state.battle_last_move1 = state.battle_ticks;
      move1 = state.battle_bot1->get_move(b1);
    }

    Move move2 = Move::NONE;
    if (!b2.game_won && b2.phase == Phase::PLAYING &&
        (state.battle_ticks - state.battle_last_move2 >=
         BOT_INPUT_TICK_RATE)) {
      state.battle_last_move2 = state.battle_ticks;
      move2 = state.battle_bot2->get_move(b2);
    }

    if (b1.phase == Phase::PLAYING)
      b1.apply_move(move1);
    if (b2.phase == Phase::PLAYING)
      b2.apply_move(move2);

    if (!b1.game_won)
      process_phases(b1, state.attacks_to_1, state.attacks_to_2,
                     state.battle_last_drop1, state.battle_last_grav1,
                     state.battle_ticks, state.battle_drop1);
    if (!b2.game_won)
      process_phases(b2, state.attacks_to_2, state.attacks_to_1,
                     state.battle_last_drop2, state.battle_last_grav2,
                     state.battle_ticks, state.battle_drop2);

    // Track first winner
    if (state.battle_first_winner == -1) {
      if (b1.game_won && b2.game_won)
        state.battle_first_winner = 0;
      else if (b1.game_won)
        state.battle_first_winner = 1;
      else if (b2.game_won)
        state.battle_first_winner = 2;
    } else {
      state.battle_loser_delta++;
    }

    state.battle_ticks++;
  }

  state.anim_frame++;
}

static void update_bot_battle_result(GameState &state) {
  int key = GetKeyPressed();
  while (key > 0) {
    if (key == KEY_ESCAPE) {
      state.scene = Scene::QUIT;
      return;
    }
    // Any other key → back to title
    init_title(state);
    return;
  }
}

// ====================== DRAWING ======================

static void draw_scene(GameState &state,
                       const LayoutMetrics &layout, float time) {

  switch (state.scene) {
  case Scene::TITLE:
    draw_title_screen(state.title_selected, state.decor_viruses, time,
                      layout.screen_w, layout.screen_h);
    break;

  case Scene::GAME:
    draw_board(state.player, layout.left_x, layout.left_y, layout.cell_size,
               time);
    draw_board(state.bot_board, layout.right_x, layout.right_y,
               layout.cell_size, time);
    draw_game_hud(state.player, state.bot_board,
                  (int)state.player_attacks.size(),
                  (int)state.bot_attacks.size(), state.wins, state.losses,
                  state.round_num, layout, time);
    break;

  case Scene::ROUND_END:
    // Draw boards in background
    draw_board(state.player, layout.left_x, layout.left_y, layout.cell_size,
               time);
    draw_board(state.bot_board, layout.right_x, layout.right_y,
               layout.cell_size, time);
    draw_game_hud(state.player, state.bot_board,
                  (int)state.player_attacks.size(),
                  (int)state.bot_attacks.size(), state.wins, state.losses,
                  state.round_num, layout, time);
    draw_round_end_screen(state.player_won, state.wins, state.losses,
                          state.round_num, layout.screen_w, layout.screen_h);
    break;

  case Scene::BOT_BATTLE:
    draw_board(state.battle_board1, layout.left_x, layout.left_y,
               layout.cell_size, time);
    draw_board(state.battle_board2, layout.right_x, layout.right_y,
               layout.cell_size, time);
    draw_bot_battle_hud(state.battle_bot1_name, state.battle_bot2_name,
                        state.battle_wins1, state.battle_wins2,
                        state.battle_trial, NUM_BATTLE_TRIALS, layout);
    break;

  case Scene::BOT_BATTLE_RESULT:
    draw_bot_battle_result_screen(state.battle_bot1_name,
                                  state.battle_bot2_name,
                                  state.result_wins1, state.result_wins2,
                                  layout.screen_w, layout.screen_h);
    break;

  case Scene::QUIT:
    break;
  }
}

// ====================== MAIN ======================

int main(int argc, char *argv[]) {
  std::srand(static_cast<unsigned>(std::time(nullptr)));
  CliArgs args = parse_args(argc, argv);

  // Init raylib
  InitWindow(SCREEN_W, SCREEN_H, "Dr. Mario");
  SetTargetFPS(TARGET_FPS);

  // Load background
  Texture2D bg = LoadTexture("bg.png");

  // Init audio
  MusicPlayer music;

  // Compute layout
  LayoutMetrics layout = compute_layout(SCREEN_W, SCREEN_H);

  // Init game state
  GameState state;

  if (args.battle) {
    init_bot_battle(state, args);
  } else {
    init_title(state);
  }

  // Main loop
  while (!WindowShouldClose() && state.scene != Scene::QUIT) {
    // Update music
    music.update();

    // Update scene
    switch (state.scene) {
    case Scene::TITLE:
      update_title(state);
      break;
    case Scene::GAME:
      update_game(state);
      break;
    case Scene::ROUND_END:
      update_round_end(state);
      break;
    case Scene::BOT_BATTLE:
      update_bot_battle(state);
      break;
    case Scene::BOT_BATTLE_RESULT:
      update_bot_battle_result(state);
      break;
    case Scene::QUIT:
      break;
    }

    // Music control
    static bool music_playing = false;
    if (state.scene == Scene::GAME && !music_playing) {
      if (music.is_available())
        music.start("queque.mp3");
      music_playing = true;
    } else if (state.scene != Scene::GAME && state.scene != Scene::BOT_BATTLE &&
               music_playing) {
      music.stop();
      music_playing = false;
    }

    // Draw
    float time = (float)GetTime();
    BeginDrawing();
    ClearBackground(BLACK);
    draw_bg(bg, layout.screen_w, layout.screen_h);
    draw_scene(state, layout, time);
    EndDrawing();
  }

  // Cleanup
  music.stop();
  UnloadTexture(bg);
  CloseWindow();

  return 0;
}
