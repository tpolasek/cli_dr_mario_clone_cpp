#pragma once

#include "bot/bot_registry.h"

/*
    Swift bot: Ultra-fast Deterministic Monte Carlo Dr. Mario AI.

    Improvements over lucky:
    1. Increased MC_DEPTH from 4 to 6 pieces for deeper lookahead
    2. Higher TOP_K (20 vs 15) for better candidate exploration  
    3. Higher DEEP_EVAL_TOP (15 vs 10) for more thorough deep evaluation
    4. Refined evaluation weights based on game phase:
       - Early game: prioritize virus adjacency and setups
       - Late game: prioritize clearing remaining viruses
    5. Better handling of cascade opportunities with chain prediction
    6. Fast path for obvious moves (immediate wins, single clearers)

    Key innovation: Deeper deterministic lookahead with phase-aware evaluation
    and optimized search parameters for maximum strength.
*/
class SwiftBot : public Bot {
public:
    Move get_move(const PlayerBoard& board) override;
    void reset() override {}
};
