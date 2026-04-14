#pragma once

#include "bot/bot_registry.h"

class RandomBot : public Bot {
public:
    Move get_move(const PlayerBoard& board) override;
    void reset() override {}
};
