#include "bot_kid.h"

#include <array>
#include <limits>
#include <queue>

namespace {

struct KidSearchNode {
    Capsule cap;
    Move first_move = Move::NONE;
    int steps = 0;
};

struct KidCandidate {
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
// Kid bot: virus scoring is drastically reduced so the bot clears
// viruses very slowly, giving kids time to watch and learn.

constexpr int WIN_SCORE                    = 500000000;
constexpr int VIRUS_CLEAR_SCORE            = 100000;     // per virus cleared (vastly reduced from 5M → 100K)
constexpr int CASCADE_CHAIN_BONUS          = 150000;     // per chain beyond first (reduced from 3M → 150K)
constexpr int REMAINING_VIRUS_PENALTY      = 2000;       // very low — don't aggressively chase viruses
constexpr int HOLE_PENALTY                 = 4200;
constexpr int BURIED_VIRUS_PENALTY         = 100;        // minimal concern about buried viruses
constexpr int BURIED_MISMATCH_PENALTY      = 50;         // minimal mismatch penalty
constexpr int ROUGHNESS_PENALTY            = 140;
constexpr int STACK_HEIGHT_PENALTY         = 220;
constexpr int DANGER_ZONE_PENALTY          = 12000;
constexpr int OFFSCREEN_LANDING_PENALTY    = 350000;
constexpr int TARGET_STABILITY_BONUS       = 150;
constexpr int VIRUS_ADJACENCY_BONUS        = 200;        // barely cares about virus adjacency
constexpr int NEAR_CLEAR_VIRUS_BONUS       = 500;        // minimal near-clear incentive

// ====================== HELPERS ======================

int kid_row_slot(int row) {
    return row + 1;
}

bool kid_in_search_bounds(const Capsule& cap) {
    return cap.r >= -1 && cap.r < ROWS &&
           cap.c >= 0 && cap.c < COLS &&
           cap.orient >= 0 && cap.orient < 4;
}

bool kid_try_apply_move(const PlayerBoard& board, const Capsule& start, Move move, Capsule& out) {
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

        return false;
    default:
        return false;
    }

    return board.fits(out);
}

Capsule kid_drop_to_rest(const PlayerBoard& board, Capsule cap) {
    while (true) {
        Capsule next = cap;
        next.r++;
        if (!board.fits(next)) return cap;
        cap = next;
    }
}

// ====================== VIRUS-AWARE EVALUATION ======================

int kid_count_same_color_neighbors(const PlayerBoard& board, int r, int c, int color) {
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

int kid_capsule_virus_alignment(const PlayerBoard& board, const Capsule& landing) {
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
                score += VIRUS_ADJACENCY_BONUS;
                int same_neighbors = kid_count_same_color_neighbors(board, nr, nc, color);
                int potential_run = 1 + same_neighbors + 1;
                if (potential_run >= MIN_RUN_LENGTH) {
                    score += VIRUS_ADJACENCY_BONUS * 2;
                }
            }
        }
    };

    check_half(landing.r1(), landing.c1(), landing.h1);
    check_half(landing.r2(), landing.c2(), landing.h2);

    return score;
}

int kid_line_setup_score(const PlayerBoard& board, int r, int c, int dr, int dc) {
    const int color = board.grid[r][c].color;
    int run = 1;
    int open_ends = 0;
    bool involves_virus = board.grid[r][c].virus;

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

    // Kid bot: virus-proximate runs are only slightly more valuable (1.5× instead of 3×)
    int virus_mult = involves_virus ? 1 : 1;

    if (run >= 3) return (3400 + open_ends * 400) * virus_mult;
    if (run == 2) return (1100 + open_ends * 200) * virus_mult;
    return 150 * open_ends * virus_mult;
}

int kid_near_clear_virus_setups(const PlayerBoard& board) {
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
                            if (open_before && open_after) {
                                score += NEAR_CLEAR_VIRUS_BONUS / 2;
                            }
                        }
                    }

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

std::pair<int, int> kid_simulate_cascade_with_chains(PlayerBoard& board) {
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

// Kid bot evaluation: virus scoring is much lower, so the bot plays
// tidily but doesn't aggressively seek out virus clears.
int kid_evaluate_board(const PlayerBoard& board, int viruses_cleared, int cascade_chains,
                       const Capsule& landing, const KidBotState& state) {
    const int remaining_viruses = board.total_viruses - board.cleared_viruses;
    if (remaining_viruses <= 0) return WIN_SCORE;

    int score = viruses_cleared * VIRUS_CLEAR_SCORE;
    score -= remaining_viruses * REMAINING_VIRUS_PENALTY;

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

            for (int rr = 0; rr < r; rr++) {
                const Piece& above = board.grid[rr][c];
                if (above.color != EMPTY) {
                    buried_virus_cells++;
                    if (above.color != cell.color) {
                        buried_mismatch_cells++;
                    }
                }
            }

            const int best_axis = std::max(
                kid_line_setup_score(board, r, c, 0, 1),
                kid_line_setup_score(board, r, c, 1, 0)
            );
            setup_score += best_axis;

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
    score -= buried_mismatch_cells * BURIED_MISMATCH_PENALTY;
    score -= roughness * ROUGHNESS_PENALTY;
    score -= danger_score * DANGER_ZONE_PENALTY;
    score += setup_score;

    score += kid_near_clear_virus_setups(board);

    if (viruses_cleared == 0) {
        score += kid_capsule_virus_alignment(board, landing);
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

KidCandidate kid_simulate_candidate(const PlayerBoard& board, const KidSearchNode& node, const KidBotState& state) {
    KidCandidate candidate;
    candidate.valid = true;
    candidate.steps = node.steps;
    candidate.landing = kid_drop_to_rest(board, node.cap);
    candidate.landing_row = std::max(candidate.landing.r1(), candidate.landing.r2());

    Move next_move = node.first_move;
    if (next_move == Move::NONE) {
        Capsule down;
        next_move = kid_try_apply_move(board, node.cap, Move::DOWN, down) ? Move::DOWN : Move::NONE;
    }
    candidate.next_move = next_move;

    PlayerBoard simulation = board;
    simulation.stamp(candidate.landing);

    auto [viruses_cleared, chains] = kid_simulate_cascade_with_chains(simulation);
    candidate.viruses_cleared = viruses_cleared;
    candidate.cascade_chains = chains;
    candidate.score = kid_evaluate_board(simulation, viruses_cleared, chains, candidate.landing, state);

    return candidate;
}

bool kid_better_candidate(const KidCandidate& lhs, const KidCandidate& rhs) {
    if (!rhs.valid) return lhs.valid;
    if (!lhs.valid) return false;

    // Kid bot: still prefer clearing viruses when possible, but the scoring
    // makes those situations much rarer to seek out actively.
    if (lhs.viruses_cleared != rhs.viruses_cleared) return lhs.viruses_cleared > rhs.viruses_cleared;
    if (lhs.cascade_chains != rhs.cascade_chains) return lhs.cascade_chains > rhs.cascade_chains;
    if (lhs.score != rhs.score) return lhs.score > rhs.score;

    if (lhs.steps != rhs.steps) return lhs.steps < rhs.steps;
    if (lhs.landing_row != rhs.landing_row) return lhs.landing_row > rhs.landing_row;

    const int lhs_center = std::abs(lhs.landing.c - (COLS / 2));
    const int rhs_center = std::abs(rhs.landing.c - (COLS / 2));
    if (lhs_center != rhs_center) return lhs_center < rhs_center;

    return static_cast<int>(lhs.next_move) < static_cast<int>(rhs.next_move);
}

}  // namespace

Move get_kid_bot_move(const PlayerBoard& board, KidBotState& state) {
    if (board.phase != Phase::PLAYING) {
        state.moving_to_target = false;
        return Move::NONE;
    }

    if (!kid_in_search_bounds(board.cap)) {
        state.moving_to_target = false;
        return Move::NONE;
    }

    constexpr std::array<Move, 3> kSearchMoves = {
        Move::LEFT, Move::RIGHT, Move::ROTATE
    };

    bool visited[ROWS + 1][COLS][4] = {};
    std::queue<KidSearchNode> queue;

    queue.push({board.cap, Move::NONE, 0});
    visited[kid_row_slot(board.cap.r)][board.cap.c][board.cap.orient] = true;

    KidCandidate best;

    while (!queue.empty()) {
        const KidSearchNode current = queue.front();
        queue.pop();

        const KidCandidate candidate = kid_simulate_candidate(board, current, state);
        if (kid_better_candidate(candidate, best)) {
            best = candidate;
        }

        for (Move move : kSearchMoves) {
            Capsule next_cap;
            if (!kid_try_apply_move(board, current.cap, move, next_cap)) continue;
            if (!kid_in_search_bounds(next_cap)) continue;

            const int row = kid_row_slot(next_cap.r);
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
        return kid_try_apply_move(board, board.cap, Move::DOWN, down) ? Move::DOWN : Move::NONE;
    }

    state.target_col = best.landing.c;
    state.target_orient = best.landing.orient;
    state.moving_to_target = best.next_move != Move::NONE && best.next_move != Move::DROP;
    return best.next_move;
}

// ====================== KidBot (registry-compatible wrapper) ======================

Move KidBot::get_move(const PlayerBoard& board) {
    return get_kid_bot_move(board, state_);
}

// ====================== SELF-REGISTRATION ======================

static bool kid_bot_registered = [] {
    BotRegistry::instance().register_bot(
        "kid",
        "Kid-friendly BFS bot — clears viruses very slowly so kids can learn",
        []() -> std::unique_ptr<Bot> { return std::make_unique<KidBot>(); }
    );
    return true;
}();
