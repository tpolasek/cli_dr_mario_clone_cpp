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
    int steps = 0;
    int landing_row = -1;
    Move next_move = Move::NONE;
    Capsule landing{};
};

constexpr int WIN_SCORE = 500000000;
constexpr int VIRUS_CLEAR_SCORE = 2500000;
constexpr int REMAINING_VIRUS_PENALTY = 110000;
constexpr int HOLE_PENALTY = 4200;
constexpr int BURIED_VIRUS_PENALTY = 300;
constexpr int ROUGHNESS_PENALTY = 140;
constexpr int STACK_HEIGHT_PENALTY = 220;
constexpr int DANGER_ZONE_PENALTY = 12000;
constexpr int OFFSCREEN_LANDING_PENALTY = 350000;
constexpr int TARGET_STABILITY_BONUS = 150;

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

int line_setup_score(const PlayerBoard& board, int r, int c, int dr, int dc) {
    const int color = board.grid[r][c].color;
    int run = 1;
    int open_ends = 0;

    int rr = r - dr;
    int cc = c - dc;
    while (rr >= 0 && rr < ROWS && cc >= 0 && cc < COLS &&
           board.grid[rr][cc].color == color) {
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
        run++;
        rr += dr;
        cc += dc;
    }
    if (rr >= 0 && rr < ROWS && cc >= 0 && cc < COLS &&
        board.grid[rr][cc].color == EMPTY) {
        open_ends++;
    }

    if (open_ends == 0) return 0;
    if (run >= 3) return 3400 + open_ends * 400;
    if (run == 2) return 1100 + open_ends * 200;
    return 150 * open_ends;
}

int evaluate_board(const PlayerBoard& board, int viruses_cleared, const Capsule& landing, const BotState& state) {
    const int remaining_viruses = board.total_viruses - board.cleared_viruses;
    if (remaining_viruses <= 0) return WIN_SCORE;

    int score = viruses_cleared * VIRUS_CLEAR_SCORE;
    score -= remaining_viruses * REMAINING_VIRUS_PENALTY;

    std::array<int, COLS> heights{};
    int topmost_filled = ROWS;
    int holes = 0;
    int roughness = 0;
    int buried_virus_cells = 0;
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

            int blocks_above = 0;
            for (int rr = 0; rr < r; rr++) {
                if (board.grid[rr][c].color != EMPTY) {
                    blocks_above++;
                }
            }
            buried_virus_cells += blocks_above;

            const int best_axis = std::max(
                line_setup_score(board, r, c, 0, 1),
                line_setup_score(board, r, c, 1, 0)
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
    score -= roughness * ROUGHNESS_PENALTY;
    score -= danger_score * DANGER_ZONE_PENALTY;
    score += setup_score;

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
    const int cleared_before = simulation.cleared_viruses;
    simulation.stamp(candidate.landing);
    simulation.simulate_cascade();
    candidate.viruses_cleared = simulation.cleared_viruses - cleared_before;
    candidate.score = evaluate_board(simulation, candidate.viruses_cleared, candidate.landing, state);

    return candidate;
}

bool better_candidate(const Candidate& lhs, const Candidate& rhs) {
    if (!rhs.valid) return lhs.valid;
    if (!lhs.valid) return false;
    if (lhs.score != rhs.score) return lhs.score > rhs.score;
    if (lhs.viruses_cleared != rhs.viruses_cleared) return lhs.viruses_cleared > rhs.viruses_cleared;
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
