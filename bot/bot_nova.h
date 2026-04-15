#pragma once

#include "bot/bot_registry.h"

/*
    Nova bot: Enhanced Dr. Mario AI.

    Key improvements over fever:
    1. Better cascade chain prediction with lookahead into gravity effects
    2. Improved board robustness metric - penalize states that are vulnerable to any piece color
    3. Combo scoring - reward setups that can chain into multiple clears
    4. Smarter candidate pruning - skip obviously bad moves earlier
    5. Weighted robustness evaluation - focus more on worst-case future scenarios
    6. Aggressive virus clearing strategy with lower thresholds for cascade pursuit
    7. Better height management with exponential penalties
*/
class NovaBot : public Bot {
public:
    Move get_move(const PlayerBoard& board) override;
    void reset() override {}
};
