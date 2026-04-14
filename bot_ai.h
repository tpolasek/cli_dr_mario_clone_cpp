#pragma once

#include "bot_registry.h"

// ====================== LEGACY API ======================
// Still available for direct use, but prefer BfsBot via the registry.

struct BotState {
    int target_col = -1;
    int target_orient = -1;
    bool moving_to_target = false;
};

Move get_bot_move(const PlayerBoard& board, BotState& state);

// ====================== BFS BOT (registry-compatible) ======================

class BfsBot : public Bot {
public:
    Move get_move(const PlayerBoard& board) override;
    void reset() override { state_ = {}; }
private:
    BotState state_;
};
