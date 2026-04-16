#pragma once

#include "bot/bot_registry.h"

/*
    Lucky bot: Deterministic Monte Carlo Dr. Mario AI.

    Strategy:
    1. BFS to enumerate all landing positions for current piece (piece 1)
    2. BFS for known next piece (piece 2) to get 2-piece combined score
    3. For top candidates, run Deterministic Monte Carlo simulation:
       - Predicts exact future piece colors using the game's RNG state
       - Simulates pieces 3-6 with full BFS placement for each piece
       - Uses the same evaluation function as fever for consistency
    4. Combines 2-piece BFS score with deterministic MC deep score

    Key innovation: Instead of averaging over all 9 color combinations
    (like fever's exhaustive 3-piece eval), Lucky uses the board's actual
    RNG state to predict the EXACT future piece colors. This gives deeper
    lookahead (6 pieces vs 3) with deterministic, noise-free evaluation.
*/
class LuckyBot : public Bot {
public:
    Move get_move(const PlayerBoard& board) override;
    void reset() override {}
};
