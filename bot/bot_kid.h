#pragma once

#include "bot/bot_registry.h"

// ====================== KID BOT ======================
// A kid-friendly variant of the BFS bot that clears viruses very slowly.
// Designed to let children learn the game at a comfortable pace.

struct KidBotState {
    int target_col = -1;
    int target_orient = -1;
    bool moving_to_target = false;
};

Move get_kid_bot_move(const PlayerBoard& board, KidBotState& state);

class KidBot : public Bot {
public:
    Move get_move(const PlayerBoard& board) override;
    void reset() override { state_ = {}; }
private:
    KidBotState state_;
};
