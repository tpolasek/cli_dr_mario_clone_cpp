#pragma once

#include "bot/bot_registry.h"

/*
    Swift bot: Deterministic Monte Carlo Dr. Mario AI.

    Improvements over lucky:
    1. Increased MC_DEPTH from 4 to 6 for deeper deterministic lookahead
       (empirically ~58-63% win rate vs lucky, up from ~55% at MC_DEPTH=5).
    2. TOP_K remains at 15 — 20 was empirically harmful because weak P1
       candidates get rescued by lucky P2 scores, polluting the combined ranking.
    3. DEEP_EVAL_TOP remains at 10 — 15 adds MC noise from weaker candidates.
    4. Phase-aware top-level bonus:
       - Early game (>50% viruses): small bonus for higher piece-1 eval (better setups)
       - Late game (<=50% viruses): bonus for virus clears on piece 1
       Applied AFTER MC evaluation to avoid perturbing the proven core evaluator.
    5. Cascade chain bonus at top level: extra reward for multi-chain cascades
       (which send garbage AND clear viruses for free).
    6. Fast paths for obvious moves: immediate wins and unique best clearers
       skip the expensive 2-piece + MC lookahead.

    Key innovation: Deeper deterministic lookahead (6 MC pieces = 8 total)
    with phase-aware and cascade-aware top-level scoring adjustments.
*/
class SwiftBot : public Bot {
public:
    Move get_move(const PlayerBoard& board) override;
    void reset() override {}
};
