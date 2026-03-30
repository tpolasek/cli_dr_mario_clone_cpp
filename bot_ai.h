#pragma once

#include "board.h"

// Persistent state for bot AI decision-making across calls.
struct BotState {
    int target_col = -1;
    int target_orient = -1;
    bool moving_to_target = false;
    int difficulty = 10;      // 1=easiest, 10=hardest
};

// Decide the next move for the bot. Does not modify the board.
Move get_bot_move(const PlayerBoard& board, BotState& state);
