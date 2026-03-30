#include "bot_ai.h"
#include <algorithm>
#include <climits>

// ====================== EVALUATION ======================

// Score a board position. Higher = better.
static int evaluate_board(const PlayerBoard& b, int viruses_cleared, int remaining_viruses) {
    // Win is always best
    if (viruses_cleared >= remaining_viruses)
        return 100000000;

    int score = 0;

    // Primary: viruses cleared this move (including cascades)
    score += viruses_cleared * 1000000;

    // Analyze remaining viruses
    int max_height = ROWS;  // highest occupied row (lowest row number = highest stack)
    int virus_proximity_score = 0;
    int num_viruses_near_top = 0;

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (b.grid[r][c].color != EMPTY && r < max_height)
                max_height = r;

            if (b.grid[r][c].virus) {
                // Track viruses dangerously near the top (overflow = game over)
                if (r <= 2) num_viruses_near_top++;

                // Count same-color neighbors in same row (horizontal run potential)
                int horiz_count = 1;  // count self
                for (int dc = -1; c + dc >= 0; dc--) {
                    if (b.grid[r][c + dc].color == b.grid[r][c].color)
                        horiz_count++;
                    else
                        break;
                }
                for (int dc = 1; c + dc < COLS; dc++) {
                    if (b.grid[r][c + dc].color == b.grid[r][c].color)
                        horiz_count++;
                    else
                        break;
                }

                // Count same-color neighbors in same column (vertical run potential)
                int vert_count = 1;  // count self
                for (int dr = -1; r + dr >= 0; dr--) {
                    if (b.grid[r + dr][c].color == b.grid[r][c].color)
                        vert_count++;
                    else
                        break;
                }
                for (int dr = 1; r + dr < ROWS; dr++) {
                    if (b.grid[r + dr][c].color == b.grid[r][c].color)
                        vert_count++;
                    else
                        break;
                }

                // Best run determines proximity to clearing
                int best_run = std::max(horiz_count, vert_count);
                if (best_run >= 4)
                    virus_proximity_score += 5000;  // one more piece clears it
                else if (best_run >= 3)
                    virus_proximity_score += 1000;
                else if (best_run >= 2)
                    virus_proximity_score += 200;
            }
        }
    }

    score += virus_proximity_score;

    // Heavy penalty for viruses near overflow
    score -= num_viruses_near_top * 50000;

    // Moderate penalty for high board (max_height is the highest occupied row, lower = higher stack)
    score -= (ROWS - max_height) * (ROWS - max_height) * 5;

    return score;
}

// ====================== PLACEMENT SIMULATION ======================

struct Placement {
    int col;
    int orient;      // 0=right 1=up 2=left 3=down
    int score;
    int drop_row;
};

// Simulate dropping a capsule from the top of a column in a given orientation.
// Returns the row where the bottom of the capsule comes to rest, or -1 if invalid.
static int simulate_drop(const PlayerBoard& b, int col, int orient, int h1, int h2) {
    // Start from row 0 or row 1 depending on orientation (up needs row >= 1)
    int start_r = (orient & 1) ? 1 : 0;

    for (int r = start_r; r < ROWS + 2; r++) {
        Capsule c;
        c.r = r; c.c = col; c.h1 = h1; c.h2 = h2; c.orient = orient;
        if (!b.fits(c)) {
            r--;
            if (r < start_r) return -1;
            return r;
        }
    }
    return -1;
}

// ====================== MAIN BOT AI ======================

Move get_bot_move(const PlayerBoard& board, BotState& state) {
    if (board.phase != Phase::PLAYING) return Move::NONE;

    // If we already have a target and are still moving toward it, continue
    if (state.moving_to_target) {
        // Check if we've reached the target
        if (board.cap.c == state.target_col && board.cap.orient == state.target_orient) {
            state.moving_to_target = false;
            return Move::DROP;
        }
    } else {
        // Find the best placement by trying all columns and orientations
        int h1 = board.cap.h1;
        int h2 = board.cap.h2;
        int remaining_viruses = board.total_viruses - board.cleared_viruses;

        Placement best = { -1, -1, INT_MIN, 0 };

        // Try all 4 orientations (not just 2!)
        for (int orient = 0; orient < 4; orient++) {
            for (int col = 0; col < COLS; col++) {
                int drop_r = simulate_drop(board, col, orient, h1, h2);
                if (drop_r < 0) continue;

                // Get final position
                Capsule c;
                c.r = drop_r; c.c = col; c.h1 = h1; c.h2 = h2; c.orient = orient;

                // Skip dead positions (piece lands at row 0 = immediate game over)
                if (c.r1() <= 0 || c.r2() <= 0) continue;

                // Simulate placement on a cloned board
                PlayerBoard sim;
                sim.clone_grid(board);
                sim.next_cap_id = board.next_cap_id;
                sim.cleared_viruses = board.cleared_viruses;
                sim.stamp(c);

                // Simulate cascades
                int vc = sim.simulate_cascade();
                int sc = evaluate_board(sim, vc, remaining_viruses);

                if (sc > best.score) {
                    best.col = col;
                    best.orient = orient;
                    best.score = sc;
                    best.drop_row = drop_r;
                }
            }
        }

        if (best.col < 0) {
            // No valid placement found (board is nearly full), try to survive
            state.target_col = board.cap.c;
            state.target_orient = board.cap.orient;
        } else {
            state.target_col = best.col;
            state.target_orient = best.orient;
        }
        state.moving_to_target = true;
    }

    // Movement toward target
    // Priority: rotate to reach target orientation, then move horizontally

    if (board.cap.orient != state.target_orient) {
        // Try rotating to reach target orientation
        // rotate() cycles: 0->1, 1->0, 2->3, 3->2 (with color swap on horizontal->vertical)
        // From any orient, we can reach any other via at most 3 rotations with wall kicks

        // Try single rotation first
        Capsule t = board.cap;
        t.rotate();
        if (board.fits(t)) return Move::ROTATE;

        // Try wall kicks
        t = board.cap; t.c--; t.rotate();
        if (board.fits(t)) { state.target_col = board.cap.c - 1; return Move::LEFT; }

        t = board.cap; t.c++; t.rotate();
        if (board.fits(t)) { state.target_col = board.cap.c + 1; return Move::RIGHT; }

        // If we can't rotate, we may need to move horizontally first to get space
    }

    // Move horizontally toward target column
    if (board.cap.c < state.target_col) {
        Capsule t = board.cap; t.c++;
        if (board.fits(t)) return Move::RIGHT;
    } else if (board.cap.c > state.target_col) {
        Capsule t = board.cap; t.c--;
        if (board.fits(t)) return Move::LEFT;
    }

    // At target column but wrong orientation - try rotation with wall kicks again
    if (board.cap.orient != state.target_orient) {
        Capsule t = board.cap;
        t.rotate();
        if (board.fits(t)) return Move::ROTATE;

        t = board.cap; t.c--; t.rotate();
        if (board.fits(t)) return Move::LEFT;

        t = board.cap; t.c++; t.rotate();
        if (board.fits(t)) return Move::RIGHT;
    }

    // Can't reach target orientation at this column - drop here
    state.moving_to_target = false;
    return Move::DROP;
}
