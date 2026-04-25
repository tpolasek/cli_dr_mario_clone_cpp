#include "game.h"
#include "constants.h"
#include <cmath>

void new_piece_with_speed(PlayerBoard &board, float &drop_speed) {
    board.new_piece();
    drop_speed = std::fmax(5, drop_speed - 1.0f / 10.0f);
}

bool process_phases(PlayerBoard &board, std::queue<int> &my_attacks,
                    std::queue<int> &opponent_attacks, int &last_drop,
                    int &last_gravity, int ticks, float &drop_speed) {
    if (board.cleared_viruses >= board.total_viruses) {
        board.game_won = true;
        return true;
    }
    switch (board.phase) {
    case Phase::PLAYING: {
        if (ticks - last_drop <= std::ceil(drop_speed))
            return false;
        last_drop = ticks;

        Capsule t = board.cap;
        t.r++;
        if (board.fits(t)) {
            board.cap = t;
            return false;
        }
        board.stamp(board.cap);

        if (board.find_and_remove_matches() > 0) {
            board.phase = Phase::GRAVITY;
            last_gravity = ticks;
            return false;
        }

        if (!my_attacks.empty()) {
            if (!board.receive_attacks(my_attacks)) {
                board.game_over = true;
                return true;
            }
            board.phase = Phase::GRAVITY;
            last_gravity = ticks;
            return false;
        }

        new_piece_with_speed(board, drop_speed);
        return false;
    }

    case Phase::GRAVITY: {
        if (ticks - last_gravity < GRAVITY_TICK_RATE)
            return false;

        last_gravity = ticks;
        if (board.gravity_step())
            return false;

        if (board.find_and_remove_matches() > 0)
            return false;

        board.flush_cascade(opponent_attacks);

        if (!my_attacks.empty()) {
            if (!board.receive_attacks(my_attacks)) {
                board.game_over = true;
                return true;
            }
            return false;
        }

        board.phase = Phase::PLAYING;
        new_piece_with_speed(board, drop_speed);
        last_drop = ticks;
        return false;
    }
    }
    return false;
}
