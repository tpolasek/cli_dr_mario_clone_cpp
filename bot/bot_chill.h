#pragma once

#include "bot/bot_registry.h"

/*
    Chill bot: Enhanced Dr. Mario AI with improved evaluation.

    Key improvements over fever:
    1. Higher cascade chain bonus (4.5M vs 3M) - cascades clear viruses AND send attacks
    2. Higher remaining virus penalty (120K vs 110K) - more urgency to clear
    3. Higher buried mismatch penalty (600 vs 400) - wrong color above virus is costly
    4. Higher stack height penalty (240 vs 220) - tall stacks are dangerous
    5. Higher virus adjacency bonus (7K vs 6K) - reward matching colors near viruses
    6. Higher near-clear virus bonus (100K vs 80K) - reward almost-complete runs
    7. Higher virus run multiplier in line_setup_score (4x vs 3x) - virus runs more valuable
*/
class ChillBot : public Bot {
public:
    Move get_move(const PlayerBoard& board) override;
    void reset() override {}
};
