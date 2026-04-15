#include "bot_ai.h"

#include <array>
#include <limits>
#include <queue>

namespace {

struct SearchNode {
    Capsule cap;
    Move first_move = Move::NONE;
    int steps = 0;
};

struct Candidate {
    bool valid = false;
    int score = std::numeric_limits<int>::min();
    int viruses_cleared = 0;
    int cascade_chains = 0;
    int steps = 0;
    int landing_row = -1;
    Move next_move = Move::NONE;
    Capsule landing{};
};

// ====================== SCORING CONSTANTS ======================
constexpr int WIN_SCORE                    = 500000000;
constexpr int VIRUS_CLEAR_SCORE            = 5000000;    // per virus cleared (was 2.5M)
constexpr int CASCADE_CHAIN_BONUS          = 3000000;    // per chain beyond first — cascades are extremely valuable
constexpr int REMAINING_VIRUS_PENALTY      = 110000;
constexpr int HOLE_PENALTY                 = 4200;
constexpr int BURIED_VIRUS_PENALTY         = 600;        // per block above a virus (was 300)
constexpr int BURIED_MISMATCH_PENALTY      = 200;        // extra penalty for burying virus under different color
constexpr int ROUGHNESS_PENALTY            = 140;
constexpr int STACK_HEIGHT_PENALTY         = 220;
constexpr int DANGER_ZONE_PENALTY          = 12000;
constexpr int OFFSCREEN_LANDING_PENALTY    = 350000;
constexpr int TARGET_STABILITY_BONUS       = 150;
constexpr int VIRUS_ADJACENCY_BONUS        = 6000;       // landing matching color adjacent to a virus
constexpr int NEAR_CLEAR_VIRUS_BONUS       = 80000;      // run of 3 (needs 4) touching a virus with open end
// NEAR_CLEAR_VIRUS_BONUS handles post-clear potential evaluation

// ====================== HELPERS ======================

int row_slot(int row) {
    return row + 1;
}

bool in_search_bounds(const Capsule& cap) {
    return cap.r >= -1 && cap.r < ROWS &&
           cap.c >= 0 && cap.c < COLS &&
           cap.orient >= 0 && cap.orient < 4;
}

bool try_apply_move(const PlayerBoard& board, const Capsule& start, Move move, Capsule& out) {
    out = start;

    switch (move) {
    case Move::LEFT:
        out.c--;
        break;
    case Move::RIGHT:
        out.c++;
        break;
    case Move::DOWN:
    case Move::DROP:
        out.r++;
        break;
    case Move::ROTATE:
        out.rotate();
        if (board.fits(out)) return true;

        out = start;
        out.c--;
        out.rotate();
        if (board.fits(out)) return true;

        out = start;
        out.c++;
        out.rotate();
        if (board.fits(out)) return true;

        out = start;
        out.r--;
        out.rotate();
        if (board.fits(out)) return true;

        return false;
    default:
        return false;
    }

    return board.fits(out);
}

Capsule drop_to_rest(const PlayerBoard& board, Capsule cap) {
    while (true) {
        Capsule next = cap;
        next.r++;
        if (!board.fits(next)) return cap;
        cap = next;
    }
}

// ====================== VIRUS-AWARE EVALUATION ======================

// Count how many same-color neighbors a cell has (for matching-color adjacency near viruses).
int count_same_color_neighbors(const PlayerBoard& board, int r, int c, int color) {
    constexpr int dr[] = {-1, 1, 0, 0};
    constexpr int dc[] = {0, 0, -1, 1};
    int count = 0;
    for (int d = 0; d < 4; d++) {
        int nr = r + dr[d], nc = c + dc[d];
        if (nr >= 0 && nr < ROWS && nc >= 0 && nc < COLS &&
            board.grid[nr][nc].color == color) {
            count++;
        }
    }
    return count;
}

// Score how well the landing capsule's colors align with adjacent viruses.
// Placing matching-color pieces next to viruses creates clearing opportunities.
int capsule_virus_alignment(const PlayerBoard& board, const Capsule& landing) {
    int score = 0;
    constexpr int dr[] = {-1, 1, 0, 0};
    constexpr int dc[] = {0, 0, -1, 1};

    auto check_half = [&](int r, int c, int color) {
        if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return;
        for (int d = 0; d < 4; d++) {
            int nr = r + dr[d], nc = c + dc[d];
            if (nr >= 0 && nr < ROWS && nc >= 0 && nc < COLS &&
                board.grid[nr][nc].virus &&
                board.grid[nr][nc].color == color) {
                // Direct adjacency to same-color virus
                score += VIRUS_ADJACENCY_BONUS;
                // Extra bonus if there's already a run building toward clearing
                int same_neighbors = count_same_color_neighbors(board, nr, nc, color);
                // The virus itself + same_neighbors + our piece = potential run length
                int potential_run = 1 + same_neighbors + 1; // virus + existing same-color + our piece
                if (potential_run >= MIN_RUN_LENGTH) {
                    score += VIRUS_ADJACENCY_BONUS * 2;  // This could complete a clear!
                }
            }
        }
    };

    check_half(landing.r1(), landing.c1(), landing.h1);
    check_half(landing.r2(), landing.c2(), landing.h2);

    return score;
}

// Evaluate runs along an axis from a starting cell.
// Enhanced to track whether the run involves a virus (much more valuable).
int line_setup_score(const PlayerBoard& board, int r, int c, int dr, int dc) {
    const int color = board.grid[r][c].color;
    int run = 1;
    int open_ends = 0;
    bool involves_virus = board.grid[r][c].virus;

    // Scan backwards
    int rr = r - dr;
    int cc = c - dc;
    while (rr >= 0 && rr < ROWS && cc >= 0 && cc < COLS &&
           board.grid[rr][cc].color == color) {
        if (board.grid[rr][cc].virus) involves_virus = true;
        run++;
        rr -= dr;
        cc -= dc;
    }
    if (rr >= 0 && rr < ROWS && cc >= 0 && cc < COLS &&
        board.grid[rr][cc].color == EMPTY) {
        open_ends++;
    }

    // Scan forwards
    rr = r + dr;
    cc = c + dc;
    while (rr >= 0 && rr < ROWS && cc >= 0 && cc < COLS &&
           board.grid[rr][cc].color == color) {
        if (board.grid[rr][cc].virus) involves_virus = true;
        run++;
        rr += dr;
        cc += dc;
    }
    if (rr >= 0 && rr < ROWS && cc >= 0 && cc < COLS &&
        board.grid[rr][cc].color == EMPTY) {
        open_ends++;
    }

    if (open_ends == 0) return 0;

    // Virus-proximate runs are 3× more valuable — they directly lead to clearing viruses
    int virus_mult = involves_virus ? 3 : 1;

    if (run >= 3) return (3400 + open_ends * 400) * virus_mult;  // run of 3 needs just 1 more
    if (run == 2) return (1100 + open_ends * 200) * virus_mult;
    return 150 * open_ends * virus_mult;
}

// Scan the board for near-clear setups that involve viruses.
// A "near-clear" is a run of exactly (MIN_RUN_LENGTH - 1) = 3 with at least one open end
// where at least one cell in the run is a virus.
int near_clear_virus_setups(const PlayerBoard& board) {
    int score = 0;

    auto check_axis = [&](bool horizontal) {
        int outer = horizontal ? ROWS : COLS;
        int inner = horizontal ? COLS : ROWS;

        for (int i = 0; i < outer; i++) {
            int run = 1;
            bool has_virus = false;
            int start_j = 0;

            for (int j = 1; j <= inner; j++) {
                int r1 = horizontal ? i : j - 1, c1 = horizontal ? j - 1 : i;
                int r2 = horizontal ? i : j,     c2 = horizontal ? j : i;

                bool same = (j < inner &&
                             board.grid[r2][c2].color != EMPTY &&
                             board.grid[r2][c2].color == board.grid[r1][c1].color);

                if (same) {
                    if (board.grid[r2][c2].virus) has_virus = true;
                    run++;
                } else {
                    if (run == (MIN_RUN_LENGTH - 1) && has_virus) {
                        // Check open ends: before the run start and after the run end
                        int before_r = horizontal ? i : start_j - 1;
                        int before_c = horizontal ? start_j - 1 : i;
                        int after_r = horizontal ? i : j;
                        int after_c = horizontal ? j : i;

                        bool open_before = (before_r >= 0 && before_r < ROWS &&
                                           before_c >= 0 && before_c < COLS &&
                                           board.grid[before_r][before_c].color == EMPTY);
                        bool open_after = (after_r >= 0 && after_r < ROWS &&
                                          after_c >= 0 && after_c < COLS &&
                                          board.grid[after_r][after_c].color == EMPTY);

                        if (open_before || open_after) {
                            score += NEAR_CLEAR_VIRUS_BONUS;
                            // Double bonus if both ends open — more flexible
                            if (open_before && open_after) {
                                score += NEAR_CLEAR_VIRUS_BONUS / 2;
                            }
                        }
                    }

                    // Start new run
                    start_j = j;
                    run = 1;
                    has_virus = false;
                    if (j < inner && board.grid[r2][c2].color != EMPTY) {
                        has_virus = board.grid[r2][c2].virus;
                    }
                }
            }
        }
    };

    check_axis(true);
    check_axis(false);
    return score;
}

// Count the number of cascade chains (clears) that happen during cascade simulation.
// Returns {total_viruses_cleared, chain_count}.
std::pair<int, int> simulate_cascade_with_chains(PlayerBoard& board) {
    int total_viruses = 0;
    int chains = 0;

    while (true) {
        int before = board.cleared_viruses;
        int removed = board.find_and_remove_matches_sim();
        int viruses_this_chain = board.cleared_viruses - before;
        total_viruses += viruses_this_chain;

        if (removed > 0) {
            chains++;
        }

        if (removed == 0 && !board.gravity_step()) {
            break;
        }
    }

    return {total_viruses, chains};
}

// Main board evaluation function.
// Enhanced with virus-focused scoring.
int evaluate_board(const PlayerBoard& board, int viruses_cleared, int cascade_chains,
                   const Capsule& landing, const BotState& state) {
    const int remaining_viruses = board.total_viruses - board.cleared_viruses;
    if (remaining_viruses <= 0) return WIN_SCORE;

    int score = viruses_cleared * VIRUS_CLEAR_SCORE;
    score -= remaining_viruses * REMAINING_VIRUS_PENALTY;

    // Cascade chains beyond the first are extremely valuable — they clear for free
    if (cascade_chains > 1) {
        score += (cascade_chains - 1) * CASCADE_CHAIN_BONUS;
    }

    std::array<int, COLS> heights{};
    int topmost_filled = ROWS;
    int holes = 0;
    int roughness = 0;
    int buried_virus_cells = 0;
    int buried_mismatch_cells = 0;
    int setup_score = 0;
    int danger_score = 0;

    for (int c = 0; c < COLS; c++) {
        bool seen_filled = false;
        int first_filled = ROWS;

        for (int r = 0; r < ROWS; r++) {
            const Piece& cell = board.grid[r][c];
            if (cell.color != EMPTY) {
                seen_filled = true;
                if (first_filled == ROWS) first_filled = r;
                if (r < topmost_filled) topmost_filled = r;
            } else if (seen_filled) {
                holes++;
            }
        }

        heights[c] = (first_filled == ROWS) ? 0 : (ROWS - first_filled);
    }

    for (int c = 1; c < COLS; c++) {
        roughness += std::abs(heights[c] - heights[c - 1]);
    }

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            const Piece& cell = board.grid[r][c];
            if (!cell.virus) continue;

            // Buried virus: count blocks above, and whether they're mismatched colors
            for (int rr = 0; rr < r; rr++) {
                const Piece& above = board.grid[rr][c];
                if (above.color != EMPTY) {
                    buried_virus_cells++;
                    if (above.color != cell.color) {
                        buried_mismatch_cells++;
                    }
                }
            }

            // Setup scoring: how well are pieces aligned to clear this virus?
            const int best_axis = std::max(
                line_setup_score(board, r, c, 0, 1),
                line_setup_score(board, r, c, 1, 0)
            );
            setup_score += best_axis;

            // Danger zone scoring
            if (r < VIRUS_FREE_ROWS) {
                const int danger = VIRUS_FREE_ROWS - r;
                danger_score += danger * danger;
            }
        }
    }

    if (topmost_filled < ROWS) {
        const int stack_height = ROWS - topmost_filled;
        score -= stack_height * stack_height * STACK_HEIGHT_PENALTY;
    }

    score -= holes * HOLE_PENALTY;
    score -= buried_virus_cells * BURIED_VIRUS_PENALTY;
    score -= buried_mismatch_cells * BURIED_MISMATCH_PENALTY;  // extra penalty for wrong-color blocks above viruses
    score -= roughness * ROUGHNESS_PENALTY;
    score -= danger_score * DANGER_ZONE_PENALTY;
    score += setup_score;

    // Near-clear virus setups on the resulting board — high priority
    score += near_clear_virus_setups(board);

    // If we didn't clear viruses, evaluate how well this placement sets up future clears
    if (viruses_cleared == 0) {
        // Capsule-virus color alignment (only matters when we don't clear — when we clear,
        // the piece is gone)
        score += capsule_virus_alignment(board, landing);
    }

    if (landing.r1() < 0 || landing.r2() < 0) {
        score -= OFFSCREEN_LANDING_PENALTY;
    }

    if (state.moving_to_target &&
        state.target_col == landing.c &&
        state.target_orient == landing.orient) {
        score += TARGET_STABILITY_BONUS;
    }

    return score;
}

Candidate simulate_candidate(const PlayerBoard& board, const SearchNode& node, const BotState& state) {
    Candidate candidate;
    candidate.valid = true;
    candidate.steps = node.steps;
    candidate.landing = drop_to_rest(board, node.cap);
    candidate.landing_row = std::max(candidate.landing.r1(), candidate.landing.r2());

    Move next_move = node.first_move;
    if (next_move == Move::NONE) {
        Capsule down;
        next_move = try_apply_move(board, node.cap, Move::DROP, down) ? Move::DROP : Move::NONE;
    }
    candidate.next_move = next_move;

    PlayerBoard simulation = board;
    simulation.stamp(candidate.landing);

    auto [viruses_cleared, chains] = simulate_cascade_with_chains(simulation);
    candidate.viruses_cleared = viruses_cleared;
    candidate.cascade_chains = chains;
    candidate.score = evaluate_board(simulation, viruses_cleared, chains, candidate.landing, state);

    return candidate;
}

bool better_candidate(const Candidate& lhs, const Candidate& rhs) {
    if (!rhs.valid) return lhs.valid;
    if (!lhs.valid) return false;

    // First priority: clear more viruses
    if (lhs.viruses_cleared != rhs.viruses_cleared) return lhs.viruses_cleared > rhs.viruses_cleared;

    // Second priority: more cascade chains (chain reactions are free clears)
    if (lhs.cascade_chains != rhs.cascade_chains) return lhs.cascade_chains > rhs.cascade_chains;

    // Third priority: overall evaluation score
    if (lhs.score != rhs.score) return lhs.score > rhs.score;

    // Tiebreakers
    if (lhs.steps != rhs.steps) return lhs.steps < rhs.steps;
    if (lhs.landing_row != rhs.landing_row) return lhs.landing_row > rhs.landing_row;

    const int lhs_center = std::abs(lhs.landing.c - (COLS / 2));
    const int rhs_center = std::abs(rhs.landing.c - (COLS / 2));
    if (lhs_center != rhs_center) return lhs_center < rhs_center;

    return static_cast<int>(lhs.next_move) < static_cast<int>(rhs.next_move);
}

}  // namespace

Move get_bot_move(const PlayerBoard& board, BotState& state) {
    if (board.phase != Phase::PLAYING) {
        state.moving_to_target = false;
        return Move::NONE;
    }

    if (!in_search_bounds(board.cap)) {
        state.moving_to_target = false;
        return Move::NONE;
    }

    constexpr std::array<Move, 4> kSearchMoves = {
        Move::LEFT, Move::RIGHT, Move::ROTATE, Move::DROP
    };

    bool visited[ROWS + 1][COLS][4] = {};
    std::queue<SearchNode> queue;

    queue.push({board.cap, Move::NONE, 0});
    visited[row_slot(board.cap.r)][board.cap.c][board.cap.orient] = true;

    Candidate best;

    while (!queue.empty()) {
        const SearchNode current = queue.front();
        queue.pop();

        const Candidate candidate = simulate_candidate(board, current, state);
        if (better_candidate(candidate, best)) {
            best = candidate;
        }

        for (Move move : kSearchMoves) {
            Capsule next_cap;
            if (!try_apply_move(board, current.cap, move, next_cap)) continue;
            if (!in_search_bounds(next_cap)) continue;

            const int row = row_slot(next_cap.r);
            if (visited[row][next_cap.c][next_cap.orient]) continue;

            visited[row][next_cap.c][next_cap.orient] = true;
            queue.push({
                next_cap,
                current.first_move == Move::NONE ? move : current.first_move,
                current.steps + 1
            });
        }
    }

    if (!best.valid) {
        state.target_col = board.cap.c;
        state.target_orient = board.cap.orient;
        state.moving_to_target = false;

        Capsule down;
        return try_apply_move(board, board.cap, Move::DROP, down) ? Move::DROP : Move::NONE;
    }

    state.target_col = best.landing.c;
    state.target_orient = best.landing.orient;
    state.moving_to_target = best.next_move != Move::NONE && best.next_move != Move::DROP;
    return best.next_move;
}

// ====================== BfsBot (registry-compatible wrapper) ======================

Move BfsBot::get_move(const PlayerBoard& board) {
    return get_bot_move(board, state_);
}

// ====================== SELF-REGISTRATION ======================

static bool bfs_bot_registered = [] {
    BotRegistry::instance().register_bot(
        "bfs",
        "BFS lookahead with virus-aware scoring",
        []() -> std::unique_ptr<Bot> { return std::make_unique<BfsBot>(); }
    );
    return true;
}();
