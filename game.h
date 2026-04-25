#pragma once

#include "board.h"
#include <queue>

// Shared game logic (extracted from terminal version).

void new_piece_with_speed(PlayerBoard &board, float &drop_speed);

// Returns true if the game ended (game_won or game_over).
bool process_phases(PlayerBoard &board, std::queue<int> &my_attacks,
                    std::queue<int> &opponent_attacks, int &last_drop,
                    int &last_gravity, int ticks, float &drop_speed);
