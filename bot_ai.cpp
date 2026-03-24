#include "bot_ai.h"
#include <algorithm>
#include <climits>
#include <cstring>

// ====================== INTERNAL TYPES ======================

struct SimBoard {
    int color[ROWS][COLS];  // EMPTY/RED/YELLOW/BLUE
    bool virus[ROWS][COLS];

    void load(const PlayerBoard& b) {
        for (int r = 0; r < ROWS; r++)
            for (int c = 0; c < COLS; c++) {
                color[r][c] = b.grid[r][c].color;
                virus[r][c] = b.grid[r][c].virus;
            }
    }

    bool cell_free(int r, int c) const {
        if (r < 0 && c >= 0 && c < COLS) return true;
        return r >= 0 && r < ROWS && c >= 0 && c < COLS && color[r][c] == EMPTY;
    }

    bool fits(int r1, int c1, int r2, int c2) const {
        return cell_free(r1, c1) && cell_free(r2, c2);
    }

    // Stamp the capsule onto the board, return row where it landed
    int stamp(int r1, int c1, int r2, int c2, int h1, int h2) {
        color[r1][c1] = h1;
        color[r2][c2] = h2;
        virus[r1][c1] = false;
        virus[r2][c2] = false;
        return std::max(r1, r2);
    }

    // Find and remove matches, return count of viruses cleared
    int find_and_remove_matches() {
        bool kill[ROWS][COLS];
        memset(kill, 0, sizeof(kill));

        auto check_runs = [&](bool horizontal) {
            int outer = horizontal ? ROWS : COLS;
            int inner = horizontal ? COLS : ROWS;
            for (int i = 0; i < outer; i++) {
                int run = 1;
                for (int j = 1; j <= inner; j++) {
                    int r1h = horizontal ? i : j - 1, c1h = horizontal ? j - 1 : i;
                    int r2h = horizontal ? i : j,     c2h = horizontal ? j : i;
                    bool same = (j < inner &&
                                 color[r2h][c2h] != EMPTY &&
                                 color[r2h][c2h] == color[r1h][c1h]);
                    if (same) {
                        run++;
                    } else {
                        if (run >= 4) {
                            for (int k = j - run; k < j; k++) {
                                int kr = horizontal ? i : k;
                                int kc = horizontal ? k : i;
                                kill[kr][kc] = true;
                            }
                        }
                        run = 1;
                    }
                }
            }
        };

        check_runs(true);
        check_runs(false);

        int virus_killed = 0, removed = 0;
        for (int r = 0; r < ROWS; r++)
            for (int c = 0; c < COLS; c++)
                if (kill[r][c]) {
                    if (virus[r][c]) virus_killed++;
                    color[r][c] = EMPTY;
                    virus[r][c] = false;
                    removed++;
                }

        return virus_killed;
    }

    // Apply gravity until settled. Returns true if anything moved.
    bool gravity_step() {
        bool moved = false;
        bool done[ROWS][COLS];
        memset(done, 0, sizeof(done));

        for (int r = ROWS - 1; r >= 0; r--) {
            for (int c = 0; c < COLS; c++) {
                if (color[r][c] == EMPTY || virus[r][c] || done[r][c])
                    continue;

                // Check if there's a piece directly above (same color, adjacent)
                // This handles vertical capsule halves
                if (r > 0 && color[r - 1][c] != EMPTY && !virus[r - 1][c] &&
                    color[r - 1][c] == color[r][c]) {
                    // Check if there's a horizontal partner
                    if (c > 0 && color[r][c - 1] == color[r][c] && !done[r][c - 1]) {
                        // horizontal pair at row r
                        if (r + 1 < ROWS && color[r + 1][c] == EMPTY && color[r + 1][c - 1] == EMPTY) {
                            color[r + 1][c] = color[r][c];
                            color[r + 1][c - 1] = color[r][c - 1];
                            color[r][c] = EMPTY;
                            color[r][c - 1] = EMPTY;
                            done[r + 1][c] = true;
                            done[r + 1][c - 1] = true;
                            moved = true;
                        }
                    } else if (c + 1 < COLS && color[r][c + 1] == color[r][c] && !done[r][c + 1]) {
                        // horizontal pair at row r
                        if (r + 1 < ROWS && color[r + 1][c] == EMPTY && color[r + 1][c + 1] == EMPTY) {
                            color[r + 1][c] = color[r][c];
                            color[r + 1][c + 1] = color[r][c + 1];
                            color[r][c] = EMPTY;
                            color[r][c + 1] = EMPTY;
                            done[r + 1][c] = true;
                            done[r + 1][c + 1] = true;
                            moved = true;
                        }
                    } else {
                        // vertical pair, both fall together
                        if (r + 1 < ROWS && color[r + 1][c] == EMPTY) {
                            color[r + 1][c] = color[r][c];
                            color[r][c] = color[r - 1][c];
                            color[r - 1][c] = EMPTY;
                            done[r + 1][c] = true;
                            done[r][c] = true;
                            moved = true;
                        }
                    }
                    continue;
                }

                // Check horizontal partner on same row
                if (c > 0 && color[r][c - 1] != EMPTY && !virus[r][c - 1] && color[r][c - 1] == color[r][c]) {
                    if (done[r][c] || done[r][c - 1]) continue;
                    if (r + 1 < ROWS && color[r + 1][c] == EMPTY && color[r + 1][c - 1] == EMPTY) {
                        color[r + 1][c] = color[r][c];
                        color[r + 1][c - 1] = color[r][c - 1];
                        color[r][c] = EMPTY;
                        color[r][c - 1] = EMPTY;
                        done[r + 1][c] = true;
                        done[r + 1][c - 1] = true;
                        moved = true;
                    }
                    continue;
                }

                if (c + 1 < COLS && color[r][c + 1] != EMPTY && !virus[r][c + 1] && color[r][c + 1] == color[r][c]) {
                    if (done[r][c] || done[r][c + 1]) continue;
                    if (r + 1 < ROWS && color[r + 1][c] == EMPTY && color[r + 1][c + 1] == EMPTY) {
                        color[r + 1][c] = color[r][c];
                        color[r + 1][c + 1] = color[r][c + 1];
                        color[r][c] = EMPTY;
                        color[r][c + 1] = EMPTY;
                        done[r + 1][c] = true;
                        done[r + 1][c + 1] = true;
                        moved = true;
                    }
                    continue;
                }

                // Single cell or top of vertical pair
                if (r + 1 < ROWS && color[r + 1][c] == EMPTY) {
                    color[r + 1][c] = color[r][c];
                    color[r][c] = EMPTY;
                    done[r + 1][c] = true;
                    moved = true;
                }
            }
        }
        return moved;
    }

    // Simulate full cascade: remove matches, gravity, repeat
    // Returns total viruses cleared across all cascades
    int simulate_cascade() {
        int total_virus_cleared = 0;
        int vc = find_and_remove_matches();
        while (vc > 0 || gravity_step()) {
            total_virus_cleared += vc;
            vc = find_and_remove_matches();
        }
        return total_virus_cleared;
    }
};

// ====================== EVALUATION ======================

// Score a board position. Higher = better.
// Strictly optimizes for virus elimination.
static int evaluate_board(const SimBoard& sb, int viruses_cleared, int total_viruses) {
    // Win is always best
    if (viruses_cleared >= total_viruses)
        return 100000000;

    int score = 0;

    // Primary: viruses cleared this move (including cascades)
    score += viruses_cleared * 1000000;

    // Count remaining viruses and how close each is to being cleared
    int max_height = 0;
    int virus_proximity_score = 0;
    int num_viruses_near_top = 0;

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (sb.color[r][c] != EMPTY && r > max_height)
                max_height = r;

            if (sb.virus[r][c]) {
                // Track viruses dangerously near the top
                if (r <= 3) num_viruses_near_top++;

                // How many same-color neighbors does this virus have?
                // (Only count non-virus pieces adjacent to it)
                int horiz_count = 0;
                int vert_count = 0;

                // Left: count contiguous same-color cells
                for (int dc = -1; c + dc >= 0; dc--) {
                    if (sb.color[r][c + dc] == sb.color[r][c])
                        horiz_count++;
                    else
                        break;
                }
                // Right
                for (int dc = 1; c + dc < COLS; dc++) {
                    if (sb.color[r][c + dc] == sb.color[r][c])
                        horiz_count++;
                    else
                        break;
                }
                // Up
                for (int dr = -1; r + dr >= 0; dr--) {
                    if (sb.color[r + dr][c] == sb.color[r][c])
                        vert_count++;
                    else
                        break;
                }
                // Down
                for (int dr = 1; r + dr < ROWS; dr++) {
                    if (sb.color[r + dr][c] == sb.color[r][c])
                        vert_count++;
                    else
                        break;
                }

                // Viruses closer to being cleared get more points
                // 3 neighbors = needs 1 more to clear (4 total)
                // 2 neighbors = needs 2 more
                // 1 neighbor  = needs 3 more
                if (horiz_count >= 3 || vert_count >= 3)
                    virus_proximity_score += 5000;  // almost there!
                else if (horiz_count >= 2 || vert_count >= 2)
                    virus_proximity_score += 1000;
                else if (horiz_count >= 1 || vert_count >= 1)
                    virus_proximity_score += 200;
            }
        }
    }

    score += virus_proximity_score;

    // Heavy penalty for viruses near overflow — means game loss soon
    score -= num_viruses_near_top * 50000;

    // Moderate penalty for high board (still matters for survival)
    score -= max_height * max_height * 5;

    return score;
}

// ====================== PLACEMENT SIMULATION ======================

struct Placement {
    int col;
    int orient;      // 0=right 1=up 2=left 3=down
    int score;
    int drop_row;    // row where piece lands (for movement)
};

// Get the two cell positions for a capsule at (r, col, orient)
static void capsule_cells(int r, int col, int orient, int h1, int h2,
                           int& r1, int& c1, int& r2, int& c2) {
    r1 = r; c1 = col;
    if (orient & 1) { r2 = r - 1; c2 = col; }
    else { r2 = r; c2 = col + 1; }
}

// Simulate dropping a capsule from the top of a column in a given orientation
// Returns the row where the bottom of the capsule comes to rest, or -1 if invalid.
static int simulate_drop(const SimBoard& sb, int col, int orient, int h1, int h2) {
    int r1, c1, r2, c2;

    // Start from row 0 or row 1 depending on orientation
    int start_r = (orient & 1) ? 1 : 0;

    for (int r = start_r; r < ROWS + 2; r++) {
        capsule_cells(r, col, orient, h1, h2, r1, c1, r2, c2);
        if (!sb.fits(r1, c1, r2, c2)) {
            // Go back one row
            r--;
            if (r < start_r) return -1; // Can't even fit at top
            return r;
        }
    }
    return -1;
}

// ====================== MAIN BOT AI ======================

void bot_ai_move(PlayerBoard& board, BotState& state) {
    if (board.phase != Phase::PLAYING) return;

    // If we already have a target and are still moving toward it, continue
    if (state.moving_to_target) {
        // Check if we've reached the target
        if (board.cap.c == state.target_col && board.cap.orient == state.target_orient) {
            state.moving_to_target = false;
            // Press down to speed up the drop
            state.down_tick = 0;  // reset counter for new piece
            Capsule t = board.cap;
            t.r++;
            if (board.fits(t)) board.cap = t;
            return;
        }
        // Continue moving toward target (see movement code below)
    } else {
        // Find the best placement
        SimBoard sb;
        sb.load(board);

        int h1 = board.cap.h1;
        int h2 = board.cap.h2;

        Placement best = { -1, -1, INT_MIN, 0 };

        // Try all columns and orientations
        for (int orient = 0; orient < 4; orient++) {
            for (int col = 0; col < COLS; col++) {
                int drop_r = simulate_drop(sb, col, orient, h1, h2);
                if (drop_r < 0) continue;

                // Get final position
                int r1, c1, r2, c2;
                capsule_cells(drop_r, col, orient, h1, h2, r1, c1, r2, c2);

                // Simulate placement
                SimBoard sim = sb;
                sim.stamp(r1, c1, r2, c2, h1, h2);

                // Simulate cascades
                int vc = sim.simulate_cascade();
                int total_viruses = board.total_viruses - board.cleared_viruses;
                int sc = evaluate_board(sim, vc, total_viruses);

                if (sc > best.score) {
                    best.col = col;
                    best.orient = orient;
                    best.score = sc;
                    best.drop_row = drop_r;
                }
            }
        }

        // Also try: swap h1 and h2 (since rotate swaps them in horizontal mode)
        // Actually, we should also evaluate swapped colors for horizontal orientations
        // But the rotation system handles this, so we're covered.

        if (best.col < 0) {
            // No valid placement found, just do something
            state.target_col = COLS / 2;
            state.target_orient = 0;
        } else {
            state.target_col = best.col;
            state.target_orient = best.orient;
        }
        state.moving_to_target = true;
    }

    // Movement toward target
    // Priority: rotate first if needed, then move horizontally

    // If we need to rotate (and we're in a horizontal orientation), try to rotate
    if (board.cap.orient != state.target_orient) {
        // Try wall-kick rotation (like the player can do)
        Capsule t = board.cap;
        t.rotate();
        if (board.fits(t)) { board.cap = t; return; }

        // Try wall kicks
        t = board.cap; t.c--; t.rotate();
        if (board.fits(t)) { board.cap = t; return; }

        t = board.cap; t.c++; t.rotate();
        if (board.fits(t)) { board.cap = t; return; }

        t = board.cap; t.r--; t.rotate();
        if (board.fits(t)) { board.cap = t; return; }
    }

    // Move horizontally toward target
    if (board.cap.c < state.target_col) {
        Capsule t = board.cap; t.c++;
        if (board.fits(t)) { board.cap = t; return; }
    } else if (board.cap.c > state.target_col) {
        Capsule t = board.cap; t.c--;
        if (board.fits(t)) { board.cap = t; return; }
    }

    // We're at the right column, maybe need one more rotation check
    if (board.cap.orient != state.target_orient) {
        Capsule t = board.cap;
        t.rotate();
        if (board.fits(t)) { board.cap = t; return; }
    }

    // Stuck or at destination
    state.moving_to_target = false;

    // Press down to speed up drop based on difficulty
    // difficulty 1 = press down every 10 ticks (slowest)
    // difficulty 10 = press down every tick (fastest)
    state.down_tick++;
    int down_interval = 11 - state.difficulty;  // 10, 9, 8, ..., 1
    if (state.down_tick >= down_interval) {
        state.down_tick = 0;
        Capsule t = board.cap;
        t.r++;
        if (board.fits(t)) board.cap = t;
    }
}
