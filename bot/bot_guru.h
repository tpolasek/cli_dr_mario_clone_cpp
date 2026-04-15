#pragma once

#include "bot/bot_registry.h"
/*
    The guru bot uses the same BFS + evaluation as the original bot for piece 1, but adds a 2-piece lookahead:
    1. Piece 1: Full BFS finds all unique landing positions, simulates cascades, evaluates boards
    2. Piece 2: For the top 5 first-piece candidates, uses a fast grid search (~30 positions) to evaluate the best outcome for the known next piece (board.nxt)
    3. Combined scoring: Picks the piece-1 placement that leads to the best 2-piece outcome, properly accounting for both pieces' virus clears and cascade chains
 */
class GuruBot : public Bot {
public:
    Move get_move(const PlayerBoard& board) override;
    void reset() override {}
};
