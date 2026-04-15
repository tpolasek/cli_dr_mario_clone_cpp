#include "bot_nova.h"
#include "constants.h"
#include <array>
#include <limits>
#include <queue>
#include <algorithm>
#include <vector>
#include <cstdint>
#include <cmath>

namespace {

// ====================== SCORING CONSTANTS ======================
constexpr int64_t WIN_SCORE                    = 500000000LL;
constexpr int64_t VIRUS_CLEAR_SCORE            = 5000000LL;
constexpr int64_t CASCADE_CHAIN_BONUS          = 3500000LL;
constexpr int64_t REMAINING_VIRUS_PENALTY      = 110000LL;
constexpr int64_t HOLE_PENALTY                 = 4500LL;
constexpr int64_t HOLE_DEPTH_WEIGHT            = 250LL;
constexpr int64_t BURIED_VIRUS_PENALTY         = 600LL;
constexpr int64_t BURIED_MISMATCH_PENALTY      = 600LL;
constexpr int64_t ROUGHNESS_PENALTY            = 150LL;
constexpr int64_t STACK_HEIGHT_PENALTY         = 250LL;
constexpr int64_t STACK_HEIGHT_CUBIC           = 8LL;
constexpr int64_t DANGER_ZONE_PENALTY          = 15000LL;
constexpr int64_t OFFSCREEN_LANDING_PENALTY    = 350000LL;
constexpr int64_t VIRUS_ADJACENCY_BONUS        = 6000LL;
constexpr int64_t NEAR_CLEAR_VIRUS_BONUS       = 80000LL;
constexpr int64_t SAME_COLOR_ABOVE_VIRUS       = 5000LL;
constexpr int64_t COLUMN_ISOLATION_PENALTY     = 150LL;
constexpr int64_t FLEXIBILITY_BONUS            = 300LL;

// Search parameters
constexpr int TOP_K                        = 18;
constexpr int PIECE3_EVAL_TOP              = 12;

constexpr std::array<Move, 4> kMoves = {{Move::LEFT, Move::RIGHT, Move::ROTATE, Move::DROP}};

// ====================== SEARCH HELPERS ======================

int row_slot(int row) { return row + 1; }

bool in_search_bounds(const Capsule& cap) {
    return cap.r >= -1 && cap.r < ROWS &&
           cap.c >= 0 && cap.c < COLS &&
           cap.orient >= 0 && cap.orient < 4;
}

bool try_apply_move(const PlayerBoard& board, const Capsule& start, Move move, Capsule& out) {
    out = start;
    switch (move) {
    case Move::LEFT:  out.c--; break;
    case Move::RIGHT: out.c++; break;
    case Move::DOWN:
    case Move::DROP:  out.r++; break;
    case Move::ROTATE:
        out.rotate();
        if (board.fits(out)) return true;
        out = start; out.c--; out.rotate();
        if (board.fits(out)) return true;
        out = start; out.c++; out.rotate();
        if (board.fits(out)) return true;
        out = start; out.r--; out.rotate();
        if (board.fits(out)) return true;
        return false;
    default: return false;
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

// ====================== CASCADE SIMULATION ======================

std::pair<int, int> simulate_cascade(PlayerBoard& board) {
    int total_viruses = 0;
    int chains = 0;
    while (true) {
        int before = board.cleared_viruses;
        int removed = board.find_and_remove_matches_sim();
        total_viruses += board.cleared_viruses - before;
        if (removed > 0) chains++;
        if (removed == 0 && !board.gravity_step()) break;
    }
    return {total_viruses, chains};
}

// ====================== EVALUATION HELPERS ======================

int count_same_color_neighbors(const PlayerBoard& board, int r, int c, int color) {
    constexpr int dr[] = {-1, 1, 0, 0};
    constexpr int dc[] = {0, 0, -1, 1};
    int count = 0;
    for (int d = 0; d < 4; d++) {
        int nr = r + dr[d], nc = c + dc[d];
        if (nr >= 0 && nr < ROWS && nc >= 0 && nc < COLS &&
            board.grid[nr][nc].color == color)
            count++;
    }
    return count;
}

int64_t capsule_virus_alignment(const PlayerBoard& board, const Capsule& landing) {
    int64_t score = 0;
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
                int same = count_same_color_neighbors(board, nr, nc, color);
                if (1 + same + 1 >= MIN_RUN_LENGTH)
                    score += VIRUS_ADJACENCY_BONUS * 2;
            }
        }
    };
    check_half(landing.r1(), landing.c1(), landing.h1);
    check_half(landing.r2(), landing.c2(), landing.h2);
    return score;
}

int64_t line_setup_score(const PlayerBoard& board, int r, int c, int dr, int dc) {
    const int color = board.grid[r][c].color;
    int run = 1;
    int open_ends = 0;
    bool involves_virus = board.grid[r][c].virus;

    int rr = r - dr, cc = c - dc;
    while (rr >= 0 && rr < ROWS && cc >= 0 && cc < COLS &&
           board.grid[rr][cc].color == color) {
        if (board.grid[rr][cc].virus) involves_virus = true;
        run++; rr -= dr; cc -= dc;
    }
    if (rr >= 0 && rr < ROWS && cc >= 0 && cc < COLS &&
        board.grid[rr][cc].color == EMPTY) open_ends++;

    rr = r + dr; cc = c + dc;
    while (rr >= 0 && rr < ROWS && cc >= 0 && cc < COLS &&
           board.grid[rr][cc].color == color) {
        if (board.grid[rr][cc].virus) involves_virus = true;
        run++; rr += dr; cc += dc;
    }
    if (rr >= 0 && rr < ROWS && cc >= 0 && cc < COLS &&
        board.grid[rr][cc].color == EMPTY) open_ends++;

    if (open_ends == 0) return 0;
    int64_t mult = involves_virus ? 3 : 1;
    if (run >= 3) return (3400LL + open_ends * 400LL) * mult;
    if (run == 2) return (1100LL + open_ends * 200LL) * mult;
    return 150LL * open_ends * mult;
}

int64_t near_clear_virus_setups(const PlayerBoard& board) {
    int64_t score = 0;
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
                            if (open_before && open_after)
                                score += NEAR_CLEAR_VIRUS_BONUS / 2;
                        }
                    }
                    start_j = j;
                    run = 1;
                    has_virus = false;
                    if (j < inner && board.grid[r2][c2].color != EMPTY)
                        has_virus = board.grid[r2][c2].virus;
                }
            }
        }
    };
    check_axis(true);
    check_axis(false);
    return score;
}

// Same-color blocks directly above viruses (builds toward clears)
int64_t same_color_above_virus_bonus(const PlayerBoard& board) {
    int64_t bonus = 0;
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            const Piece& cell = board.grid[r][c];
            if (!cell.virus) continue;
            for (int rr = r - 1; rr >= 0; rr--) {
                const Piece& above = board.grid[rr][c];
                if (above.color == EMPTY) break;
                if (above.color == cell.color) {
                    bonus += SAME_COLOR_ABOVE_VIRUS;
                } else {
                    break;
                }
            }
        }
    }
    return bonus;
}

// Column isolation: penalize tall columns next to short ones
int64_t column_isolation_penalty(const std::array<int, COLS>& heights) {
    int64_t penalty = 0;
    for (int c = 0; c < COLS; c++) {
        int left = (c > 0) ? heights[c - 1] : heights[c];
        int right = (c < COLS - 1) ? heights[c + 1] : heights[c];
        int avg = (left + right) / 2;
        int diff = heights[c] - avg;
        if (diff > 0) penalty += (int64_t)diff * diff * COLUMN_ISOLATION_PENALTY;
    }
    return penalty;
}

// Flexibility bonus: reward having empty columns for future piece placement
int64_t flexibility_score(const PlayerBoard& board) {
    int empty_cols = 0;
    for (int c = 0; c < COLS; c++) {
        if (board.grid[ROWS - 1][c].color == EMPTY) {
            empty_cols++;
        }
    }
    return (int64_t)empty_cols * FLEXIBILITY_BONUS;
}

// ====================== MAIN EVALUATION FUNCTION ======================

int64_t evaluate_board(const PlayerBoard& board, int viruses_cleared, int cascade_chains,
                       const Capsule& landing) {
    const int remaining = board.total_viruses - board.cleared_viruses;
    if (remaining <= 0) return WIN_SCORE;

    int64_t score = (int64_t)viruses_cleared * VIRUS_CLEAR_SCORE;
    score -= (int64_t)remaining * REMAINING_VIRUS_PENALTY;
    if (cascade_chains > 1) score += (int64_t)(cascade_chains - 1) * CASCADE_CHAIN_BONUS;

    std::array<int, COLS> heights{};
    std::array<bool, COLS> col_seen{};
    std::array<int, COLS> blocks_above_col{};
    int topmost = ROWS;
    int64_t hole_penalty = 0;
    int roughness = 0;
    int buried_virus = 0;
    int buried_mismatch = 0;
    int64_t setup = 0;
    int danger = 0;
    int64_t same_color_above_bonus = 0;
    int empty_bottom_cols = 0;

    // Combined single-pass grid scan for heights, holes, per-virus analysis,
    // and same-color-above-virus bonus
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            const Piece& cell = board.grid[r][c];
            if (cell.color != EMPTY) {
                if (!col_seen[c]) {
                    col_seen[c] = true;
                    heights[c] = ROWS - r;
                    if (r < topmost) topmost = r;
                }
                blocks_above_col[c]++;
            } else if (col_seen[c]) {
                hole_penalty += HOLE_PENALTY + (int64_t)blocks_above_col[c] * HOLE_DEPTH_WEIGHT;
            }

            if (cell.virus) {
                int consec_same = 0;
                bool consec_broken = false;
                for (int rr = r - 1; rr >= 0; rr--) {
                    const Piece& above = board.grid[rr][c];
                    if (above.color == EMPTY) break;
                    buried_virus++;
                    if (above.color != cell.color) {
                        buried_mismatch++;
                        consec_broken = true;
                    } else if (!consec_broken) {
                        consec_same++;
                    }
                }
                same_color_above_bonus += consec_same * SAME_COLOR_ABOVE_VIRUS;

                setup += std::max(
                    line_setup_score(board, r, c, 0, 1),
                    line_setup_score(board, r, c, 1, 0)
                );
                if (r < VIRUS_FREE_ROWS) {
                    int d = VIRUS_FREE_ROWS - r;
                    danger += d * d;
                }
            }
        }
    }

    // Flexibility: count empty columns (bottom row)
    for (int c = 0; c < COLS; c++)
        if (board.grid[ROWS - 1][c].color == EMPTY) empty_bottom_cols++;

    // Roughness: compute from heights
    for (int c = 1; c < COLS; c++)
        roughness += std::abs(heights[c] - heights[c - 1]);

    // Stack height penalty (quadratic + cubic)
    if (topmost < ROWS) {
        int h = ROWS - topmost;
        score -= (int64_t)h * h * STACK_HEIGHT_PENALTY;
        score -= (int64_t)h * h * h * STACK_HEIGHT_CUBIC;
    }

    score -= hole_penalty;
    score -= (int64_t)buried_virus * BURIED_VIRUS_PENALTY;
    score -= (int64_t)buried_mismatch * BURIED_MISMATCH_PENALTY;
    score -= (int64_t)roughness * ROUGHNESS_PENALTY;
    score -= (int64_t)danger * DANGER_ZONE_PENALTY;
    score += setup;
    score += near_clear_virus_setups(board);
    score += same_color_above_bonus;
    score -= column_isolation_penalty(heights);
    score += (int64_t)empty_bottom_cols * FLEXIBILITY_BONUS;

    if (viruses_cleared == 0)
        score += capsule_virus_alignment(board, landing);

    if (landing.r1() < 0 || landing.r2() < 0)
        score -= OFFSCREEN_LANDING_PENALTY;

    return score;
}

// ====================== CANDIDATE STRUCT ======================

struct SearchNode {
    Capsule cap;
    Move first_move = Move::NONE;
    int steps = 0;
};

struct Candidate {
    bool valid = false;
    int64_t score = std::numeric_limits<int64_t>::min();
    int viruses_cleared = 0;
    int cascade_chains = 0;
    int steps = 0;
    int landing_row = -1;
    Move next_move = Move::NONE;
    Capsule landing{};
    PlayerBoard board_after{};
};

bool better_candidate(const Candidate& lhs, const Candidate& rhs) {
    if (!rhs.valid) return lhs.valid;
    if (!lhs.valid) return false;
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

// ====================== BFS FOR PIECE 1 ======================

std::vector<Candidate> bfs_eval(const PlayerBoard& board) {
    std::vector<Candidate> results;

    bool visited[ROWS + 1][COLS][4] = {};
    bool landing_seen[ROWS + 1][COLS][4] = {};
    std::queue<SearchNode> queue;

    queue.push({board.cap, Move::NONE, 0});
    visited[row_slot(board.cap.r)][board.cap.c][board.cap.orient] = true;

    while (!queue.empty()) {
        SearchNode current = queue.front();
        queue.pop();

        Capsule landing = drop_to_rest(board, current.cap);
        int landing_row = std::max(landing.r1(), landing.r2());

        Move next_move = current.first_move;
        if (next_move == Move::NONE) {
            Capsule down;
            next_move = try_apply_move(board, board.cap, Move::DROP, down) ? Move::DROP : Move::NONE;
        }

        bool dup = false;
        if (landing.r >= -1 && landing.r < ROWS) {
            int lr = row_slot(landing.r);
            dup = landing_seen[lr][landing.c][landing.orient];
            if (!dup) landing_seen[lr][landing.c][landing.orient] = true;
        }

        if (!dup) {
            PlayerBoard sim = board;
            sim.stamp(landing);
            auto [vc, chains] = simulate_cascade(sim);
            int64_t score = evaluate_board(sim, vc, chains, landing);

            results.push_back({true, score, vc, chains, current.steps, landing_row,
                              next_move, landing, std::move(sim)});
        }

        for (Move move : kMoves) {
            Capsule next_cap;
            if (!try_apply_move(board, current.cap, move, next_cap)) continue;
            if (!in_search_bounds(next_cap)) continue;
            int row = row_slot(next_cap.r);
            if (visited[row][next_cap.c][next_cap.orient]) continue;
            visited[row][next_cap.c][next_cap.orient] = true;
            queue.push({next_cap,
                        current.first_move == Move::NONE ? move : current.first_move,
                        current.steps + 1});
        }
    }

    std::sort(results.begin(), results.end(), better_candidate);
    return results;
}

// ====================== FULL BFS FOR PIECE 2 ======================

struct QuickResult {
    int64_t score = std::numeric_limits<int64_t>::min();
    int vc = 0;
    int chains = 0;
    PlayerBoard board_after{};
};

QuickResult bfs_piece2_eval(const PlayerBoard& board) {
    QuickResult best;

    bool visited[ROWS + 1][COLS][4] = {};
    bool landing_seen[ROWS + 1][COLS][4] = {};
    std::queue<Capsule> queue;

    queue.push(board.cap);
    visited[row_slot(board.cap.r)][board.cap.c][board.cap.orient] = true;

    while (!queue.empty()) {
        Capsule current = queue.front();
        queue.pop();

        Capsule landing = drop_to_rest(board, current);

        bool dup = false;
        if (landing.r >= -1 && landing.r < ROWS) {
            int lr = row_slot(landing.r);
            dup = landing_seen[lr][landing.c][landing.orient];
            if (!dup) landing_seen[lr][landing.c][landing.orient] = true;
        }

        if (!dup) {
            PlayerBoard sim = board;
            sim.stamp(landing);
            auto [vc, chains] = simulate_cascade(sim);
            int64_t score = evaluate_board(sim, vc, chains, landing);

            bool better = false;
            if (vc > best.vc) better = true;
            else if (vc == best.vc && chains > best.chains) better = true;
            else if (vc == best.vc && chains == best.chains && score > best.score) better = true;

            if (better) {
                best.score = score;
                best.vc = vc;
                best.chains = chains;
                best.board_after = sim;
            }
        }

        for (Move move : kMoves) {
            Capsule next_cap;
            if (!try_apply_move(board, current, move, next_cap)) continue;
            if (!in_search_bounds(next_cap)) continue;
            int row = row_slot(next_cap.r);
            if (visited[row][next_cap.c][next_cap.orient]) continue;
            visited[row][next_cap.c][next_cap.orient] = true;
            queue.push(next_cap);
        }
    }

    return best;
}

// ====================== ROBUSTNESS EVALUATION ======================
// Grid search version - much faster than BFS for piece 2 in robustness eval

QuickResult grid_search_eval(const PlayerBoard& board) {
    QuickResult best;

    Capsule cap = board.cap;
    for (int o = 0; o < 4; o++) {
        int max_c = ((o & 1) == 0) ? COLS - 1 : COLS;
        for (int c = 0; c < max_c; c++) {
            cap.c = c;
            cap.r = 0;
            if (!board.fits(cap)) continue;

            Capsule landing = drop_to_rest(board, cap);
            PlayerBoard sim = board;
            sim.stamp(landing);
            auto [vc, chains] = simulate_cascade(sim);
            int64_t score = evaluate_board(sim, vc, chains, landing);

            bool better = false;
            if (vc > best.vc) better = true;
            else if (vc == best.vc && chains > best.chains) better = true;
            else if (vc == best.vc && chains == best.chains && score > best.score) better = true;

            if (better) {
                best.score = score;
                best.vc = vc;
                best.chains = chains;
                best.board_after = sim;
            }
        }
        cap.rotate();
    }

    return best;
}

struct RobustnessResult {
    int64_t avg_score = 0;
    int64_t min_score = std::numeric_limits<int64_t>::max();
};

RobustnessResult eval_future_robustness(const PlayerBoard& board) {
    RobustnessResult result;
    int64_t total = 0;
    int count = 0;

    for (int h1 = RED; h1 <= BLUE; h1++) {
        for (int h2 = RED; h2 <= BLUE; h2++) {
            PlayerBoard board3 = board;
            board3.cap.h1 = h1;
            board3.cap.h2 = h2;
            board3.cap.r = 0;
            board3.cap.c = COLS / 2 - 1;
            board3.cap.orient = 0;

            if (!board3.fits(board3.cap)) {
                total -= 200'000'000LL;
                result.min_score = std::min<int64_t>(result.min_score, -200'000'000LL);
                count++;
                continue;
            }

            // Use grid search instead of BFS for speed
            QuickResult qr = grid_search_eval(board3);
            total += qr.score;
            result.min_score = std::min(result.min_score, qr.score);
            count++;
        }
    }

    result.avg_score = (count > 0) ? total / count : std::numeric_limits<int64_t>::min();
    return result;
}

} // anonymous namespace

// ====================== NOVA BOT ======================

Move NovaBot::get_move(const PlayerBoard& board) {
    if (board.phase != Phase::PLAYING) return Move::NONE;
    if (!in_search_bounds(board.cap)) return Move::NONE;

    // Step 1: Full BFS for piece 1
    auto candidates = bfs_eval(board);
    if (candidates.empty()) {
        Capsule down;
        return try_apply_move(board, board.cap, Move::DROP, down) ? Move::DROP : Move::NONE;
    }

    // If best move wins immediately, take it
    if (candidates[0].board_after.cleared_viruses >= candidates[0].board_after.total_viruses) {
        return candidates[0].next_move;
    }

    // If only one candidate clears viruses and no ties, take it
    int best_vc = candidates[0].viruses_cleared;
    int best_chains = candidates[0].cascade_chains;

    if (best_vc > 0) {
        int same_count = 0;
        for (auto& c : candidates) {
            if (c.viruses_cleared == best_vc && c.cascade_chains == best_chains)
                same_count++;
            else break;
        }
        if (same_count <= 1) {
            return candidates[0].next_move;
        }
    }

    // Step 2: 2-piece lookahead with full BFS for piece 2
    int k = std::min((int)candidates.size(), TOP_K);

    struct Piece1Result {
        int idx;
        int64_t combined_score;
        QuickResult piece2;
    };
    std::vector<Piece1Result> piece1_results;

    for (int i = 0; i < k; i++) {
        Candidate& first = candidates[i];

        PlayerBoard board2 = first.board_after;
        board2.cap = board.nxt;

        if (!board2.fits(board2.cap)) {
            int64_t score = (int64_t)first.viruses_cleared * VIRUS_CLEAR_SCORE - 200'000'000LL;
            piece1_results.push_back({i, score, {}});
            continue;
        }

        QuickResult second = bfs_piece2_eval(board2);

        int64_t combined = (int64_t)first.viruses_cleared * VIRUS_CLEAR_SCORE
                         + (int64_t)(first.cascade_chains > 1 ? (first.cascade_chains - 1) * CASCADE_CHAIN_BONUS : 0)
                         + second.score;

        piece1_results.push_back({i, combined, second});
    }

    // Sort by combined score
    std::sort(piece1_results.begin(), piece1_results.end(),
              [](const Piece1Result& a, const Piece1Result& b) {
                  return a.combined_score > b.combined_score;
              });

    // Step 3: For top candidates, evaluate 3-piece robustness
    int eval_count = std::min((int)piece1_results.size(), PIECE3_EVAL_TOP);

    int best_idx = piece1_results[0].idx;
    int64_t best_3piece_score = std::numeric_limits<int64_t>::min();

    for (int i = 0; i < eval_count; i++) {
        Piece1Result& p1r = piece1_results[i];
        Candidate& first = candidates[p1r.idx];

        if (first.board_after.cleared_viruses >= first.board_after.total_viruses) {
            if (p1r.combined_score > best_3piece_score) {
                best_3piece_score = p1r.combined_score;
                best_idx = p1r.idx;
            }
            continue;
        }

        if (p1r.piece2.board_after.cleared_viruses >= p1r.piece2.board_after.total_viruses) {
            int64_t score = p1r.combined_score + WIN_SCORE;
            if (score > best_3piece_score) {
                best_3piece_score = score;
                best_idx = p1r.idx;
            }
            continue;
        }

        // Evaluate robustness with worst-case weighting
        RobustnessResult rob = eval_future_robustness(p1r.piece2.board_after);

        // Blend average and worst-case: prefer boards that are good on average
        // AND don't have terrible worst cases
        int64_t robustness = rob.avg_score + (rob.min_score / 2);

        // Extra penalty if worst case is very bad (potential game-over)
        if (rob.min_score < -100'000'000LL) {
            robustness -= 50'000'000LL;
        }

        int64_t total = p1r.combined_score + robustness;

        if (total > best_3piece_score) {
            best_3piece_score = total;
            best_idx = p1r.idx;
        }
    }

    return candidates[best_idx].next_move;
}

// ====================== SELF-REGISTRATION ======================

static bool nova_bot_registered = [] {
    BotRegistry::instance().register_bot(
        "nova",
        "3-piece lookahead with worst-case robustness and enhanced evaluation",
        []() -> std::unique_ptr<Bot> { return std::make_unique<NovaBot>(); }
    );
    return true;
}();
