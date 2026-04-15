#pragma once

#include "bot/bot_registry.h"

/*
    Fever bot: Enhanced 2-piece lookahead Dr. Mario AI.

    Improvements over guru:
    1. Full BFS for piece 2 (guru uses limited grid search that misses wall-kick positions)
    2. Higher TOP_K (12 vs 5) for more first-piece candidates
    3. Depth-weighted hole penalty (deeper holes penalized more)
    4. Same-color-above-virus bonus (reward building vertical runs toward viruses)
    5. Harsher buried mismatch penalty (wrong color above virus is very bad)
    6. Cubic stack danger penalty (exponential height penalty for tall stacks)
    7. Column isolation penalty (penalize tall columns surrounded by short ones)
*/
class FeverBot : public Bot {
public:
    Move get_move(const PlayerBoard& board) override;
    void reset() override {}
};
