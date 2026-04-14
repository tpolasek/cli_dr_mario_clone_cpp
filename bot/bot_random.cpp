#include "bot_random.h"
#include "constants.h"
#include <cstdlib>

Move RandomBot::get_move(const PlayerBoard& board) {
    if (board.phase != Phase::PLAYING) return Move::NONE;

    // Pick a random move from the set of legal moves
    constexpr Move candidates[] = {Move::LEFT, Move::RIGHT, Move::ROTATE, Move::DROP};

    // 50% chance to just drop — keeps things fast
    if (std::rand() % 2 == 0) return Move::DROP;

    int idx = std::rand() % 4;
    return candidates[idx];
}

// ====================== SELF-REGISTRATION ======================

static bool random_bot_registered = [] {
    BotRegistry::instance().register_bot(
        "random",
        "Picks random moves (easy difficulty)",
        []() -> std::unique_ptr<Bot> { return std::make_unique<RandomBot>(); }
    );
    return true;
}();
