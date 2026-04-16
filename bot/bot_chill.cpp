#include "bot_chill.h"
#include "constants.h"
#include <array>
#include <limits>
#include <queue>
#include <algorithm>
#include <vector>
#include <climits>

namespace {

// ====================== SCORING CONSTANTS ======================
// Based on fever with targeted evaluation improvements
constexpr int WIN_SCORE                    = 500000000;
constexpr int VIRUS_CLEAR_SCORE            = 5000000;
constexpr int CASCADE_CHAIN_BONUS          = 4500000;    // was 3000000 - cascades are doubly valuable
constexpr int REMAINING_VIRUS_PENALTY      = 120000;     // was 110000 - each remaining virus costs more
constexpr int HOLE_PENALTY                 = 4200;
constexpr int BURIED_VIRUS_PENALTY         = 600;
constexpr int BURIED_MISMATCH_PENALTY      = 600;        // was 400
constexpr int ROUGHNESS_PENALTY            = 140;
constexpr int STACK_HEIGHT_PENALTY         = 240;        // was 220 - tall stacks are more dangerous
constexpr int DANGER_ZONE_PENALTY          = 12000;
constexpr int OFFSCREEN_LANDING_PENALTY    = 350000;
constexpr int VIRUS_ADJACENCY_BONUS        = 7000;       // was 6000
constexpr int NEAR_CLEAR_VIRUS_BONUS       = 100000;     // was 80000

constexpr int TOP_K                        = 15;
constexpr int PIECE3_EVAL_TOP              = 10;

constexpr std::array<Move, 4> kMoves = {{Move::LEFT, Move::RIGHT, Move::ROTATE, Move::DROP}};

int row_slot(int row) { return row + 1; }
bool in_search_bounds(const Capsule& cap) {
    return cap.r >= -1 && cap.r < ROWS && cap.c >= 0 && cap.c < COLS && cap.orient >= 0 && cap.orient < 4;
}
bool try_apply_move(const PlayerBoard& board, const Capsule& start, Move move, Capsule& out) {
    out = start;
    switch (move) {
    case Move::LEFT:  out.c--; break;
    case Move::RIGHT: out.c++; break;
    case Move::DOWN: case Move::DROP:  out.r++; break;
    case Move::ROTATE:
        out.rotate(); if (board.fits(out)) return true;
        out = start; out.c--; out.rotate(); if (board.fits(out)) return true;
        out = start; out.c++; out.rotate(); if (board.fits(out)) return true;
        out = start; out.r--; out.rotate(); if (board.fits(out)) return true;
        return false;
    default: return false;
    }
    return board.fits(out);
}
Capsule drop_to_rest(const PlayerBoard& board, Capsule cap) {
    while (true) { Capsule next = cap; next.r++; if (!board.fits(next)) return cap; cap = next; }
}
std::pair<int, int> simulate_cascade(PlayerBoard& board) {
    int total_viruses = 0, chains = 0;
    while (true) {
        int before = board.cleared_viruses;
        int removed = board.find_and_remove_matches_sim();
        total_viruses += board.cleared_viruses - before;
        if (removed > 0) chains++;
        if (removed == 0 && !board.gravity_step()) break;
    }
    return {total_viruses, chains};
}

int count_same_color_neighbors(const PlayerBoard& board, int r, int c, int color) {
    constexpr int dr[] = {-1, 1, 0, 0};
    constexpr int dc[] = {0, 0, -1, 1};
    int count = 0;
    for (int d = 0; d < 4; d++) {
        int nr = r + dr[d], nc = c + dc[d];
        if (nr >= 0 && nr < ROWS && nc >= 0 && nc < COLS && board.grid[nr][nc].color == color) count++;
    }
    return count;
}

int capsule_virus_alignment(const PlayerBoard& board, const Capsule& landing) {
    int score = 0;
    constexpr int dr[] = {-1, 1, 0, 0};
    constexpr int dc[] = {0, 0, -1, 1};
    auto check_half = [&](int r, int c, int color) {
        if (r < 0 || r >= ROWS || c < 0 || c >= COLS) return;
        for (int d = 0; d < 4; d++) {
            int nr = r + dr[d], nc = c + dc[d];
            if (nr >= 0 && nr < ROWS && nc >= 0 && nc < COLS &&
                board.grid[nr][nc].virus && board.grid[nr][nc].color == color) {
                score += VIRUS_ADJACENCY_BONUS;
                int same = count_same_color_neighbors(board, nr, nc, color);
                if (1 + same + 1 >= MIN_RUN_LENGTH) score += VIRUS_ADJACENCY_BONUS * 2;
            }
        }
    };
    check_half(landing.r1(), landing.c1(), landing.h1);
    check_half(landing.r2(), landing.c2(), landing.h2);
    return score;
}

int line_setup_score(const PlayerBoard& board, int r, int c, int dr, int dc) {
    const int color = board.grid[r][c].color;
    int run = 1, open_ends = 0;
    bool involves_virus = board.grid[r][c].virus;
    int rr = r - dr, cc = c - dc;
    while (rr >= 0 && rr < ROWS && cc >= 0 && cc < COLS && board.grid[rr][cc].color == color) {
        if (board.grid[rr][cc].virus) involves_virus = true;
        run++; rr -= dr; cc -= dc;
    }
    if (rr >= 0 && rr < ROWS && cc >= 0 && cc < COLS && board.grid[rr][cc].color == EMPTY) open_ends++;
    rr = r + dr; cc = c + dc;
    while (rr >= 0 && rr < ROWS && cc >= 0 && cc < COLS && board.grid[rr][cc].color == color) {
        if (board.grid[rr][cc].virus) involves_virus = true;
        run++; rr += dr; cc += dc;
    }
    if (rr >= 0 && rr < ROWS && cc >= 0 && cc < COLS && board.grid[rr][cc].color == EMPTY) open_ends++;
    if (open_ends == 0) return 0;
    int mult = involves_virus ? 4 : 1;  // was 3
    if (run >= 3) return (3400 + open_ends * 400) * mult;
    if (run == 2) return (1100 + open_ends * 200) * mult;
    return 150 * open_ends * mult;
}

int near_clear_virus_setups(const PlayerBoard& board) {
    int score = 0;
    auto check_axis = [&](bool horizontal) {
        int outer = horizontal ? ROWS : COLS;
        int inner = horizontal ? COLS : ROWS;
        for (int i = 0; i < outer; i++) {
            int run = 1; bool has_virus = false; int start_j = 0;
            for (int j = 1; j <= inner; j++) {
                int r1 = horizontal ? i : j - 1, c1 = horizontal ? j - 1 : i;
                int r2 = horizontal ? i : j,     c2 = horizontal ? j : i;
                bool same = (j < inner && board.grid[r2][c2].color != EMPTY &&
                             board.grid[r2][c2].color == board.grid[r1][c1].color);
                if (same) { if (board.grid[r2][c2].virus) has_virus = true; run++; }
                else {
                    if (run == (MIN_RUN_LENGTH - 1) && has_virus) {
                        int before_r = horizontal ? i : start_j - 1, before_c = horizontal ? start_j - 1 : i;
                        int after_r = horizontal ? i : j, after_c = horizontal ? j : i;
                        bool ob = (before_r >= 0 && before_r < ROWS && before_c >= 0 && before_c < COLS &&
                                   board.grid[before_r][before_c].color == EMPTY);
                        bool oa = (after_r >= 0 && after_r < ROWS && after_c >= 0 && after_c < COLS &&
                                   board.grid[after_r][after_c].color == EMPTY);
                        if (ob || oa) { score += NEAR_CLEAR_VIRUS_BONUS; if (ob && oa) score += NEAR_CLEAR_VIRUS_BONUS / 2; }
                    }
                    start_j = j; run = 1; has_virus = false;
                    if (j < inner && board.grid[r2][c2].color != EMPTY) has_virus = board.grid[r2][c2].virus;
                }
            }
        }
    };
    check_axis(true); check_axis(false);
    return score;
}

int evaluate_board(const PlayerBoard& board, int viruses_cleared, int cascade_chains, const Capsule& landing) {
    const int remaining = board.total_viruses - board.cleared_viruses;
    if (remaining <= 0) return WIN_SCORE;
    int score = viruses_cleared * VIRUS_CLEAR_SCORE;
    score -= remaining * REMAINING_VIRUS_PENALTY;
    if (cascade_chains > 1) score += (cascade_chains - 1) * CASCADE_CHAIN_BONUS;
    std::array<int, COLS> heights{}; std::array<bool, COLS> col_seen{};
    int topmost = ROWS, holes = 0, roughness = 0, buried_virus = 0, buried_mismatch = 0, setup = 0, danger = 0;
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            const Piece& cell = board.grid[r][c];
            if (cell.color != EMPTY) { if (!col_seen[c]) { col_seen[c] = true; heights[c] = ROWS - r; if (r < topmost) topmost = r; } }
            else if (col_seen[c]) { holes++; }
            if (cell.virus) {
                for (int rr = 0; rr < r; rr++) { const Piece& above = board.grid[rr][c]; if (above.color != EMPTY) { buried_virus++; if (above.color != cell.color) buried_mismatch++; } }
                setup += std::max(line_setup_score(board, r, c, 0, 1), line_setup_score(board, r, c, 1, 0));
                if (r < VIRUS_FREE_ROWS) { int d = VIRUS_FREE_ROWS - r; danger += d * d; }
            }
        }
    }
    for (int c = 1; c < COLS; c++) roughness += std::abs(heights[c] - heights[c - 1]);
    if (topmost < ROWS) { int h = ROWS - topmost; score -= h * h * STACK_HEIGHT_PENALTY; }
    score -= holes * HOLE_PENALTY;
    score -= buried_virus * BURIED_VIRUS_PENALTY;
    score -= buried_mismatch * BURIED_MISMATCH_PENALTY;
    score -= roughness * ROUGHNESS_PENALTY;
    score -= danger * DANGER_ZONE_PENALTY;
    score += setup;
    score += near_clear_virus_setups(board);
    if (viruses_cleared == 0) score += capsule_virus_alignment(board, landing);
    if (landing.r1() < 0 || landing.r2() < 0) score -= OFFSCREEN_LANDING_PENALTY;
    return score;
}

struct SearchNode { Capsule cap; Move first_move = Move::NONE; int steps = 0; };
struct Candidate {
    bool valid = false; int score = INT_MIN; int viruses_cleared = 0;
    int cascade_chains = 0; int steps = 0; int landing_row = -1; Move next_move = Move::NONE;
    Capsule landing{}; PlayerBoard board_after{};
};
bool better_candidate(const Candidate& lhs, const Candidate& rhs) {
    if (!rhs.valid) return lhs.valid; if (!lhs.valid) return false;
    if (lhs.viruses_cleared != rhs.viruses_cleared) return lhs.viruses_cleared > rhs.viruses_cleared;
    if (lhs.cascade_chains != rhs.cascade_chains) return lhs.cascade_chains > rhs.cascade_chains;
    if (lhs.score != rhs.score) return lhs.score > rhs.score;
    if (lhs.steps != rhs.steps) return lhs.steps < rhs.steps;
    if (lhs.landing_row != rhs.landing_row) return lhs.landing_row > rhs.landing_row;
    const int lc = std::abs(lhs.landing.c - (COLS / 2)), rc = std::abs(rhs.landing.c - (COLS / 2));
    if (lc != rc) return lc < rc;
    return static_cast<int>(lhs.next_move) < static_cast<int>(rhs.next_move);
}

std::vector<Candidate> bfs_eval(const PlayerBoard& board) {
    std::vector<Candidate> results;
    bool visited[ROWS + 1][COLS][4] = {}, landing_seen[ROWS + 1][COLS][4] = {};
    std::queue<SearchNode> queue;
    queue.push({board.cap, Move::NONE, 0});
    visited[row_slot(board.cap.r)][board.cap.c][board.cap.orient] = true;
    while (!queue.empty()) {
        SearchNode current = queue.front(); queue.pop();
        Capsule landing = drop_to_rest(board, current.cap);
        int landing_row = std::max(landing.r1(), landing.r2());
        Move next_move = current.first_move;
        if (next_move == Move::NONE) { Capsule down; next_move = try_apply_move(board, board.cap, Move::DROP, down) ? Move::DROP : Move::NONE; }
        bool dup = false;
        if (landing.r >= -1 && landing.r < ROWS) { int lr = row_slot(landing.r); dup = landing_seen[lr][landing.c][landing.orient]; if (!dup) landing_seen[lr][landing.c][landing.orient] = true; }
        if (!dup) {
            PlayerBoard sim = board; sim.stamp(landing);
            auto [vc, chains] = simulate_cascade(sim);
            int score = evaluate_board(sim, vc, chains, landing);
            results.push_back({true, score, vc, chains, current.steps, landing_row, next_move, landing, std::move(sim)});
        }
        for (Move move : kMoves) {
            Capsule next_cap;
            if (!try_apply_move(board, current.cap, move, next_cap)) continue;
            if (!in_search_bounds(next_cap)) continue;
            int row = row_slot(next_cap.r);
            if (visited[row][next_cap.c][next_cap.orient]) continue;
            visited[row][next_cap.c][next_cap.orient] = true;
            queue.push({next_cap, current.first_move == Move::NONE ? move : current.first_move, current.steps + 1});
        }
    }
    std::sort(results.begin(), results.end(), better_candidate);
    return results;
}

struct QuickResult { int score = INT_MIN; int vc = 0; int chains = 0; PlayerBoard board_after{}; };

QuickResult bfs_piece2_eval(const PlayerBoard& board) {
    QuickResult best;
    bool visited[ROWS + 1][COLS][4] = {}, landing_seen[ROWS + 1][COLS][4] = {};
    std::queue<Capsule> queue;
    queue.push(board.cap);
    visited[row_slot(board.cap.r)][board.cap.c][board.cap.orient] = true;
    while (!queue.empty()) {
        Capsule current = queue.front(); queue.pop();
        Capsule landing = drop_to_rest(board, current);
        bool dup = false;
        if (landing.r >= -1 && landing.r < ROWS) { int lr = row_slot(landing.r); dup = landing_seen[lr][landing.c][landing.orient]; if (!dup) landing_seen[lr][landing.c][landing.orient] = true; }
        if (!dup) {
            PlayerBoard sim = board; sim.stamp(landing);
            auto [vc, chains] = simulate_cascade(sim);
            int score = evaluate_board(sim, vc, chains, landing);
            bool better = false;
            if (vc > best.vc) better = true;
            else if (vc == best.vc && chains > best.chains) better = true;
            else if (vc == best.vc && chains == best.chains && score > best.score) better = true;
            if (better) { best.score = score; best.vc = vc; best.chains = chains; best.board_after = sim; }
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

int64_t eval_future_robustness(const PlayerBoard& board) {
    int64_t total = 0; int count = 0;
    for (int h1 = RED; h1 <= BLUE; h1++) {
        for (int h2 = RED; h2 <= BLUE; h2++) {
            PlayerBoard board3 = board;
            board3.cap.h1 = h1; board3.cap.h2 = h2; board3.cap.r = 0; board3.cap.c = COLS / 2 - 1; board3.cap.orient = 0;
            if (!board3.fits(board3.cap)) { total -= 200'000'000; count++; continue; }
            QuickResult result = bfs_piece2_eval(board3);
            total += result.score; count++;
        }
    }
    return (count > 0) ? total / count : (std::numeric_limits<int64_t>::min)();
}

} // anonymous namespace

Move ChillBot::get_move(const PlayerBoard& board) {
    if (board.phase != Phase::PLAYING) return Move::NONE;
    if (!in_search_bounds(board.cap)) return Move::NONE;
    auto candidates = bfs_eval(board);
    if (candidates.empty()) { Capsule down; return try_apply_move(board, board.cap, Move::DROP, down) ? Move::DROP : Move::NONE; }
    if (candidates[0].board_after.cleared_viruses >= candidates[0].board_after.total_viruses) return candidates[0].next_move;
    int best_vc = candidates[0].viruses_cleared, best_chains = candidates[0].cascade_chains;
    if (best_vc > 0) {
        int same_count = 0;
        for (auto& c : candidates) { if (c.viruses_cleared == best_vc && c.cascade_chains == best_chains) same_count++; else break; }
        if (same_count <= 1) return candidates[0].next_move;
    }
    int k = std::min((int)candidates.size(), (int)TOP_K);
    struct Piece1Result { int idx; int64_t combined_score; QuickResult piece2; };
    std::vector<Piece1Result> piece1_results;
    for (int i = 0; i < k; i++) {
        Candidate& first = candidates[i];
        PlayerBoard board2 = first.board_after; board2.cap = board.nxt;
        if (!board2.fits(board2.cap)) { Piece1Result pr{ i, (int64_t)first.viruses_cleared * VIRUS_CLEAR_SCORE - 200'000'000LL, {}}; piece1_results.push_back(pr); continue; }
        QuickResult second = bfs_piece2_eval(board2);
        int64_t combined = (int64_t)first.viruses_cleared * VIRUS_CLEAR_SCORE
                         + (int64_t)(first.cascade_chains > 1 ? (first.cascade_chains - 1) * CASCADE_CHAIN_BONUS : 0)
                         + (int64_t)second.score;
        piece1_results.push_back({i, combined, second});
    }
    std::sort(piece1_results.begin(), piece1_results.end(), [](const Piece1Result& a, const Piece1Result& b) { return a.combined_score > b.combined_score; });
    int eval_count = std::min((int)piece1_results.size(), (int)PIECE3_EVAL_TOP);
    int best_idx = piece1_results[0].idx;
    int64_t best_3piece_score = LLONG_MIN;
    for (int i = 0; i < eval_count; i++) {
        Piece1Result& p1r = piece1_results[i];
        Candidate& first = candidates[p1r.idx];
        if (first.board_after.cleared_viruses >= first.board_after.total_viruses) { if (p1r.combined_score > best_3piece_score) { best_3piece_score = p1r.combined_score; best_idx = p1r.idx; } continue; }
        if (p1r.piece2.board_after.cleared_viruses >= p1r.piece2.board_after.total_viruses) { int64_t s = p1r.combined_score + WIN_SCORE; if (s > best_3piece_score) { best_3piece_score = s; best_idx = p1r.idx; } continue; }
        int64_t robustness = eval_future_robustness(p1r.piece2.board_after);
        int64_t total = p1r.combined_score + robustness;
        if (total > best_3piece_score) { best_3piece_score = total; best_idx = p1r.idx; }
    }
    return candidates[best_idx].next_move;
}

static bool chill_bot_registered = [] {
    BotRegistry::instance().register_bot("chill", "3-piece BFS with tuned virus targeting",
        []() -> std::unique_ptr<Bot> { return std::make_unique<ChillBot>(); });
    return true;
}();
