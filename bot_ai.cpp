#include "bot_ai.h"
#include <algorithm>
#include <climits>
#include <cstring>

// ====================== INTERNAL TYPES ======================

struct SimBoard {
    int color[ROWS][COLS];  // EMPTY/RED/YELLOW/BLUE
    bool virus[ROWS][COLS];
    int capId[ROWS][COLS];  // 0=virus/empty, >0=capsule id

    void load(const PlayerBoard& b) {
        for (int r = 0; r < ROWS; r++)
            for (int c = 0; c < COLS; c++) {
                color[r][c] = b.grid[r][c].color;
                virus[r][c] = b.grid[r][c].virus;
                capId[r][c] = b.grid[r][c].capId;
            }
    }

    bool cell_free(int r, int c) const {
        if (r < 0 && c >= 0 && c < COLS) return true;
        return r >= 0 && r < ROWS && c >= 0 && c < COLS && color[r][c] == EMPTY;
    }

    bool fits(int r1, int c1, int r2, int c2) const {
        return cell_free(r1, c1) && cell_free(r2, c2);
    }

    // Stamp the capsule onto the board
    void stamp(int r1, int c1, int r2, int c2, int h1, int h2, int id) {
        color[r1][c1] = h1;
        virus[r1][c1] = false;
        capId[r1][c1] = id;
        color[r2][c2] = h2;
        virus[r2][c2] = false;
        capId[r2][c2] = id;
    }

    // Check if two cells are capsule partners (same capId, adjacent)
    bool is_partner(int r, int c, int dr, int dc, int id) const {
        int nr = r + dr, nc = c + dc;
        return nr >= 0 && nr < ROWS && nc >= 0 && nc < COLS &&
               color[nr][nc] != EMPTY &&
               !virus[nr][nc] &&
               capId[nr][nc] == id;
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
                    int r1 = horizontal ? i : j - 1, c1 = horizontal ? j - 1 : i;
                    int r2 = horizontal ? i : j,     c2 = horizontal ? j : i;
                    bool same = (j < inner &&
                                 color[r2][c2] != EMPTY &&
                                 color[r2][c2] == color[r1][c1]);
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

        int virus_killed = 0;
        for (int r = 0; r < ROWS; r++)
            for (int c = 0; c < COLS; c++)
                if (kill[r][c]) {
                    if (virus[r][c]) virus_killed++;
                    color[r][c] = EMPTY;
                    virus[r][c] = false;
                    capId[r][c] = 0;
                }

        return virus_killed;
    }

    // Apply gravity until settled. Returns true if anything moved.
    // Uses capId to correctly identify capsule pairs (not color).
    bool gravity_step() {
        bool moved = false;
        bool done[ROWS][COLS];
        memset(done, 0, sizeof(done));

        for (int r = ROWS - 1; r >= 0; r--) {
            for (int c = 0; c < COLS; c++) {
                if (color[r][c] == EMPTY || virus[r][c] || done[r][c])
                    continue;

                int id = capId[r][c];

                // Check if partner is directly above (vertical pair)
                if (is_partner(r, c, -1, 0, id)) {
                    // Vertical pair falls together
                    if (r + 1 < ROWS && color[r + 1][c] == EMPTY) {
                        color[r + 1][c] = color[r][c];
                        virus[r + 1][c] = virus[r][c];
                        capId[r + 1][c] = id;
                        color[r][c] = color[r - 1][c];
                        virus[r][c] = virus[r - 1][c];
                        capId[r][c] = id;
                        color[r - 1][c] = EMPTY;
                        virus[r - 1][c] = false;
                        capId[r - 1][c] = 0;
                        done[r + 1][c] = true;
                        done[r][c] = true;
                        moved = true;
                    }
                    continue;
                }

                // Check if partner is below (we're top of vertical pair, skip)
                if (is_partner(r, c, 1, 0, id))
                    continue;

                // Check horizontal partners
                int dc = 0;
                if (is_partner(r, c, 0, -1, id)) dc = -1;
                else if (is_partner(r, c, 0, 1, id)) dc = 1;

                if (dc != 0) {
                    int c2 = c + dc;
                    if (done[r][c] || done[r][c2]) continue;
                    if (r + 1 < ROWS &&
                        color[r + 1][c] == EMPTY &&
                        color[r + 1][c2] == EMPTY) {
                        color[r + 1][c] = color[r][c];
                        virus[r + 1][c] = virus[r][c];
                        capId[r + 1][c] = id;
                        color[r + 1][c2] = color[r][c2];
                        virus[r + 1][c2] = virus[r][c2];
                        capId[r + 1][c2] = id;
                        color[r][c] = EMPTY;
                        virus[r][c] = false;
                        capId[r][c] = 0;
                        color[r][c2] = EMPTY;
                        virus[r][c2] = false;
                        capId[r][c2] = 0;
                        done[r + 1][c] = true;
                        done[r + 1][c2] = true;
                        moved = true;
                    }
                } else {
                    // Single cell (orphan from broken match)
                    if (r + 1 < ROWS && color[r + 1][c] == EMPTY) {
                        color[r + 1][c] = color[r][c];
                        virus[r + 1][c] = virus[r][c];
                        capId[r + 1][c] = capId[r][c];
                        color[r][c] = EMPTY;
                        virus[r][c] = false;
                        capId[r][c] = 0;
                        done[r + 1][c] = true;
                        moved = true;
                    }
                }
            }
        }
        return moved;
    }

    // Simulate full cascade: remove matches, gravity, repeat
    // Returns total viruses cleared across all cascades
    int simulate_cascade() {
        int total_virus_cleared = 0;
        while (true) {
            int vc = find_and_remove_matches();
            total_virus_cleared += vc;
            if (vc == 0 && !gravity_step())
                break;
        }
        return total_virus_cleared;
    }
};

// ====================== EVALUATION ======================

// Score a board position. Higher = better.
static int evaluate_board(const SimBoard& sb, int viruses_cleared, int remaining_viruses) {
    // Win is always best
    if (viruses_cleared >= remaining_viruses)
        return 100000000;

    int score = 0;

    // Primary: viruses cleared this move (including cascades)
    score += viruses_cleared * 1000000;

    // Analyze remaining viruses
    int max_height = 0;
    int virus_proximity_score = 0;
    int num_viruses_near_top = 0;

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (sb.color[r][c] != EMPTY && r > max_height)
                max_height = r;

            if (sb.virus[r][c]) {
                // Track viruses dangerously near the top (overflow = game over)
                if (r <= 2) num_viruses_near_top++;

                // Count same-color neighbors in same row (horizontal run potential)
                int horiz_count = 1;  // count self
                for (int dc = -1; c + dc >= 0; dc--) {
                    if (sb.color[r][c + dc] == sb.color[r][c])
                        horiz_count++;
                    else
                        break;
                }
                for (int dc = 1; c + dc < COLS; dc++) {
                    if (sb.color[r][c + dc] == sb.color[r][c])
                        horiz_count++;
                    else
                        break;
                }

                // Count same-color neighbors in same column (vertical run potential)
                int vert_count = 1;  // count self
                for (int dr = -1; r + dr >= 0; dr--) {
                    if (sb.color[r + dr][c] == sb.color[r][c])
                        vert_count++;
                    else
                        break;
                }
                for (int dr = 1; r + dr < ROWS; dr++) {
                    if (sb.color[r + dr][c] == sb.color[r][c])
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

    // Moderate penalty for high board
    score -= max_height * max_height * 5;

    return score;
}

// ====================== PLACEMENT SIMULATION ======================

struct Placement {
    int col;
    int orient;      // 0=right 1=up 2=left 3=down
    int score;
    int drop_row;
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

    // Start from row 0 or row 1 depending on orientation (up needs row >= 1)
    int start_r = (orient & 1) ? 1 : 0;

    for (int r = start_r; r < ROWS + 2; r++) {
        capsule_cells(r, col, orient, h1, h2, r1, c1, r2, c2);
        if (!sb.fits(r1, c1, r2, c2)) {
            r--;
            if (r < start_r) return -1;
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
            state.down_tick = 0;
            // Speed up drop
            Capsule t = board.cap;
            t.r++;
            if (board.fits(t)) board.cap = t;
            return;
        }
        // Continue moving toward target (see movement code below)
    } else {
        // Find the best placement by trying all columns and orientations
        SimBoard sb;
        sb.load(board);

        int h1 = board.cap.h1;
        int h2 = board.cap.h2;
        int remaining_viruses = board.total_viruses - board.cleared_viruses;

        Placement best = { -1, -1, INT_MIN, 0 };

        // Try all 4 orientations (not just 2!)
        for (int orient = 0; orient < 4; orient++) {
            for (int col = 0; col < COLS; col++) {
                int drop_r = simulate_drop(sb, col, orient, h1, h2);
                if (drop_r < 0) continue;

                // Get final position
                int r1, c1, r2, c2;
                capsule_cells(drop_r, col, orient, h1, h2, r1, c1, r2, c2);

                // Skip dead positions (piece lands at row 0 = immediate game over)
                if (r1 <= 0 || r2 <= 0) continue;

                // Simulate placement
                SimBoard sim = sb;
                sim.stamp(r1, c1, r2, c2, h1, h2, board.next_cap_id);

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
        if (board.fits(t)) { board.cap = t; return; }

        // Try wall kicks
        t = board.cap; t.c--; t.rotate();
        if (board.fits(t)) { board.cap = t; return; }

        t = board.cap; t.c++; t.rotate();
        if (board.fits(t)) { board.cap = t; return; }

        t = board.cap; t.r--; t.rotate();
        if (board.fits(t)) { board.cap = t; return; }

        // If we can't rotate, we may need to move horizontally first to get space
    }

    // Move horizontally toward target column
    if (board.cap.c < state.target_col) {
        Capsule t = board.cap; t.c++;
        if (board.fits(t)) { board.cap = t; return; }
    } else if (board.cap.c > state.target_col) {
        Capsule t = board.cap; t.c--;
        if (board.fits(t)) { board.cap = t; return; }
    }

    // At target column but wrong orientation - try rotation with wall kicks again
    if (board.cap.orient != state.target_orient) {
        Capsule t = board.cap;
        t.rotate();
        if (board.fits(t)) { board.cap = t; return; }

        t = board.cap; t.c--; t.rotate();
        if (board.fits(t)) { board.cap = t; return; }

        t = board.cap; t.c++; t.rotate();
        if (board.fits(t)) { board.cap = t; return; }

        t = board.cap; t.r--; t.rotate();
        if (board.fits(t)) { board.cap = t; return; }
    }

    // Can't reach target orientation at this column - just drop here
    state.moving_to_target = false;

    // Press down to speed up drop based on difficulty
    state.down_tick++;
    int down_interval = 11 - state.difficulty;  // 10, 9, 8, ..., 1
    if (state.down_tick >= down_interval) {
        state.down_tick = 0;
        Capsule t = board.cap;
        t.r++;
        if (board.fits(t)) board.cap = t;
    }
}
