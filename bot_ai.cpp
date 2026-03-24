#include "bot_ai.h"
#include <cstdlib>

void bot_ai_move(PlayerBoard& board, BotState& state) {
    if (board.phase != Phase::PLAYING) return;

    if (!state.moving_to_target) {
        state.target_col = std::rand() % (COLS - 1);
        state.target_orient = std::rand() % 4;
        state.moving_to_target = true;
    }

    // Rotate toward target orientation
    if (board.cap.orient != state.target_orient) {
        Capsule t = board.cap;
        t.rotate();
        if (board.fits(t)) { board.cap = t; return; }
    }

    // Move toward target column
    if (board.cap.c < state.target_col) {
        Capsule t = board.cap; t.c++;
        if (board.fits(t)) { board.cap = t; return; }
    } else if (board.cap.c > state.target_col) {
        Capsule t = board.cap; t.c--;
        if (board.fits(t)) { board.cap = t; return; }
    }

    state.moving_to_target = false;
}
