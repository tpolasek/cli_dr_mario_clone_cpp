#pragma once

#include "board.h"

// Persistent state for bot AI decision-making across calls.
struct BotState {
    int target_col = -1;
    int target_orient = -1;
    bool moving_to_target = false;
    int down_tick = 0;        // counter for controlling down-press frequency
    int difficulty = 10;      // 1=easiest, 10=hardest (controls drop speed)
};

// Performs one step of the bot AI.
// Modifies board.cap in-place if a move/rotation is valid.
void bot_ai_move(PlayerBoard& board, BotState& state);
