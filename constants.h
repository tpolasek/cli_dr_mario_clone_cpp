#pragma once

// ====================== BOARD ======================
constexpr int ROWS = 16;
constexpr int COLS  = 8;

// ====================== COLORS ======================
constexpr int EMPTY  = 0;
constexpr int RED    = 1;
constexpr int YELLOW = 2;
constexpr int BLUE   = 3;

// ====================== GAMEPLAY ======================
constexpr int MIN_RUN_LENGTH     = 4;   // minimum same-color run to clear
constexpr int MAX_ATTACKS        = 4;   // max garbage pieces received at once
constexpr int VIRUS_FREE_ROWS    = 4;   // top rows that never contain viruses
constexpr int MIN_CASCADE_ATTACK = 2;   // cascade chain length to trigger attack
constexpr int SCORE_PER_PIECE    = 10;  // score awarded per cleared piece

// ====================== RNG ======================
constexpr unsigned int RNG_MULT = 1103515245u;
constexpr unsigned int RNG_INC  = 12345u;
constexpr unsigned int RNG_MASK = 0x7FFFu;

// ====================== TIMING ======================
constexpr int BOT_INPUT_TICK_RATE = 10;
constexpr int GRAVITY_TICK_RATE   = 20;
constexpr int GAME_FPS            = 60;
