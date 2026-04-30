#include "ui_renderer.h"
#include "ui_theme.h"
#include "constants.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

// ====================== HELPERS ======================

static const char *color_char(int c) {
    switch (c) {
    case RED:    return "r";
    case YELLOW: return "y";
    case BLUE:   return "b";
    default:     return "r";
    }
}

// Build a sprite name into a stack buffer
template <int N> struct SpriteName {
    char buf[N];
    SpriteName(const char *fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
    }
    operator const char *() const { return buf; }
};

// ====================== DECORATIVE VIRUSES ======================

std::vector<DecorVirus> generate_decor_viruses(int count, int sw, int sh) {
    std::vector<DecorVirus> viruses;
    for (int i = 0; i < count; i++) {
        DecorVirus v;
        v.x = (float)(std::rand() % sw);
        v.y = (float)(std::rand() % sh);
        v.color = 1 + (std::rand() % 3);
        v.phase = (float)(std::rand() % 1000) / 10.0f;
        v.size = 15.0f + (float)(std::rand() % 25);
        viruses.push_back(v);
    }
    return viruses;
}

// ====================== LAYOUT ======================

LayoutMetrics compute_layout(int sw, int sh) {
    LayoutMetrics m;
    m.screen_w = sw;
    m.screen_h = sh;

    // Cell size from vertical fit
    float board_vert_pct = 0.82f;
    m.cell_size = (int)(sh * board_vert_pct) / ROWS;
    m.board_w = COLS * m.cell_size;
    m.board_h = ROWS * m.cell_size;
    m.board_y = (sh - m.board_h) / 2;

    // Side panel width — enough for NEXT preview + padding
    m.panel_w = (int)(sw * 0.078f); // ~150px at 1920

    // Margin from screen edges
    int margin = (int)(sw * 0.026f); // ~50px at 1920

    // Gap between panel and board
    int gap = std::max(1, (int)(sw * 0.005f)); // ~10px at 1920

    // Left side panel + board
    m.left_panel_x = margin;
    m.left_board_x = margin + m.panel_w + gap;

    // Right board + side panel — cap center gap so boards are closer
    int left_block = margin + m.panel_w + gap + m.board_w;
    int right_block = m.board_w + gap + m.panel_w + margin;
    int max_center_gap = (int)(sw * 0.12f); // ~230px at 1920
    int natural_center_gap = sw - left_block - right_block;
    int center_gap = std::min(natural_center_gap, max_center_gap);

    // Center the entire layout horizontally
    int total_width = left_block + center_gap + right_block;
    int x_offset = (sw - total_width) / 2;

    m.left_panel_x = x_offset;
    m.left_board_x = x_offset + m.panel_w + gap;
    m.right_board_x = m.left_board_x + m.board_w + center_gap;
    m.right_panel_x = m.right_board_x + m.board_w + gap;

    // Center HUD fills the gap between boards
    m.center_x = m.left_board_x + m.board_w;
    m.center_w = m.right_board_x - m.center_x;

    return m;
}

// ====================== CONSTRUCTOR ======================

UIRenderer::UIRenderer(Gfx &gfx) : gfx_(gfx), sprites_(nullptr) {}

void UIRenderer::set_sprite_sheet(const SpriteSheet *sprites) {
    sprites_ = sprites;
}

// ====================== BACKGROUND ======================

void UIRenderer::draw_bg(Gfx::Tex bg_tex, int sw, int sh) {
    if (bg_tex != Gfx::NO_TEX) {
        gfx_.draw_texture(bg_tex, 0, 0, sw, sh);
    } else {
        gfx_.clear(Theme::BG_DEEP);
    }
}

// ====================== VIRUS SPRITE ======================

void UIRenderer::draw_virus_sprite(int cx, int cy, float radius, int color,
                                   float time, float phase) {
    // --- Sprite path ---
    if (sprites_) {
        int frame = ((int)(time * 2.0f + phase) % 2) + 1;
        SpriteName<64> name("virus_%s_%d", color_char(color), frame);
        const SpriteRegion *sr = sprites_->get(name);
        if (sr) {
            int size = (int)(radius * 2);
            gfx_.draw_texture_region(sprites_->texture(),
                                     sr->x, sr->y, sr->w, sr->h,
                                     cx - size / 2, cy - size / 2,
                                     size, size);
            return;
        }
    }

    // --- Primitive fallback ---
    UIColor body = Theme::virus_body(color);
    UIColor legs = Theme::virus_legs(color);

    float wiggle = sinf(time * 3.0f + phase) * 0.35f;
    float leg_len = radius * 0.5f;

    for (int i = 0; i < 4; i++) {
        float base_angle = 3.14159265f / 4.0f + (float)i * 3.14159265f / 2.0f;
        float angle = base_angle + wiggle;
        float start_dist = radius * 0.6f;
        float sx = cx + cosf(angle) * start_dist;
        float sy = cy + sinf(angle) * start_dist;
        float ex = cx + cosf(angle) * (start_dist + leg_len);
        float ey = cy + sinf(angle) * (start_dist + leg_len);
        gfx_.draw_line((int)sx, (int)sy, (int)ex, (int)ey, legs);
    }

    int body_r = (int)(radius * 0.62f);
    gfx_.draw_circle(cx, cy, body_r, body);

    int eye_size = std::max(2, (int)(body_r * 0.35f));
    int pupil_size = std::max(1, eye_size / 2);
    int eye_spacing = (int)(body_r * 0.45f);
    int eye_y = cy - (int)(body_r * 0.15f);

    int lex = cx - eye_spacing - eye_size / 2;
    int ley = eye_y - eye_size / 2;
    gfx_.draw_rect(lex, ley, eye_size, eye_size, Theme::EYE_WHITE);
    gfx_.draw_rect(lex + pupil_size, ley + pupil_size / 2, pupil_size,
                   pupil_size, Theme::EYE_PUPIL);

    int rex = cx + eye_spacing - eye_size / 2;
    gfx_.draw_rect(rex, ley, eye_size, eye_size, Theme::EYE_WHITE);
    gfx_.draw_rect(rex + pupil_size, ley + pupil_size / 2, pupil_size,
                   pupil_size, Theme::EYE_PUPIL);
}

// ====================== PILL CELL (BEVELED — PRIMITIVE FALLBACK) ======================

void UIRenderer::draw_pill_cell(int x, int y, int w, int h, int color,
                                bool draw_top, bool draw_bottom,
                                bool draw_left, bool draw_right) {
    UIColor fill = Theme::pill_color(color);
    UIColor lt = Theme::pill_light(color);
    UIColor dk = Theme::pill_dark(color);

    gfx_.draw_rect(x, y, w, h, fill);

    if (draw_top)
        gfx_.draw_line(x, y, x + w - 1, y, lt);
    if (draw_left)
        gfx_.draw_line(x, y, x, y + h - 1, lt);
    if (draw_bottom)
        gfx_.draw_line(x, y + h - 1, x + w - 1, y + h - 1, dk);
    if (draw_right)
        gfx_.draw_line(x + w - 1, y, x + w - 1, y + h - 1, dk);
}

// ====================== DRAW A SINGLE CAPSULE CELL SPRITE ======================

void UIRenderer::draw_single_cap_sprite(int dst_x, int dst_y, int dst_w,
                                        int dst_h, int color) {
    SpriteName<32> name("cap_%s", color_char(color));
    const SpriteRegion *sr = sprites_->get(name);
    if (sr) {
        gfx_.draw_texture_region(sprites_->texture(),
                                 sr->x, sr->y, sr->w, sr->h,
                                 dst_x, dst_y, dst_w, dst_h);
        return;
    }
    // Primitive fallback — draw as beveled cell with all edges
    draw_pill_cell(dst_x, dst_y, dst_w, dst_h, color, true, true, true, true);
}

// ====================== DRAW HORIZONTAL CAPSULE SPRITE ======================

bool UIRenderer::draw_hcap_sprite(int dst_x, int dst_y, int dst_w, int dst_h,
                                  int h1, int h2) {
    if (!sprites_)
        return false;

    // Try exact name: cap_{h1}_{h2}
    SpriteName<32> name("cap_%s_%s", color_char(h1), color_char(h2));
    const SpriteRegion *sr = sprites_->get(name);
    if (sr) {
        gfx_.draw_texture_region(sprites_->texture(),
                                 sr->x, sr->y, sr->w, sr->h,
                                 dst_x, dst_y, dst_w, dst_h);
        return true;
    }

    // Try reversed: cap_{h2}_{h1} drawn mirrored — not supported without
    // flip, so fall back to two singles
    return false;
}

// ====================== DRAW A VERTICAL CAPSULE SPRITE ======================

bool UIRenderer::draw_vcap_sprite(int dst_x, int dst_y, int dst_w, int dst_h,
                                  int top_color, int bottom_color) {
    if (!sprites_)
        return false;

    SpriteName<48> name("cap_%s_%s_vertical",
                        color_char(top_color), color_char(bottom_color));
    const SpriteRegion *sr = sprites_->get(name);
    if (sr) {
        gfx_.draw_texture_region(sprites_->texture(),
                                 sr->x, sr->y, sr->w, sr->h,
                                 dst_x, dst_y, dst_w, dst_h);
        return true;
    }
    return false;
}

// ====================== ACTIVE CAPSULE ======================

void UIRenderer::draw_active_capsule(const Capsule &cap, int bx, int by,
                                     int cs) {
    int pad = 1;
    int r1 = cap.r1(), c1 = cap.c1();
    int r2 = cap.r2(), c2 = cap.c2();
    bool horizontal = (r1 == r2);

    if (horizontal) {
        int min_c = std::min(c1, c2);
        int max_c = std::max(c1, c2);
        int left_color = (min_c == c1) ? cap.h1 : cap.h2;
        int right_color = (max_c == c1) ? cap.h1 : cap.h2;

        int x = bx + min_c * cs + pad;
        int y = by + r1 * cs + pad;
        int full_w = (max_c - min_c + 1) * cs - pad * 2;
        int h = cs - pad * 2;

        // Try the horizontal sprite first
        if (draw_hcap_sprite(x, y, full_w, h, left_color, right_color))
            return;

        // Fallback: two halves (sprite or primitive)
        int half_w = full_w / 2;
        if (sprites_) {
            draw_single_cap_sprite(x, y, half_w, h, left_color);
            draw_single_cap_sprite(x + half_w, y, half_w, h, right_color);
        } else {
            draw_pill_cell(x, y, half_w, h, left_color,
                           true, true, true, false);
            draw_pill_cell(x + half_w, y, half_w, h, right_color,
                           true, true, false, true);
        }
    } else {
        // Vertical
        int min_r = std::min(r1, r2);
        int max_r = std::max(r1, r2);
        int top_color = (min_r == r1) ? cap.h1 : cap.h2;
        int bottom_color = (max_r == r1) ? cap.h1 : cap.h2;

        int x = bx + c1 * cs + pad;
        int y = by + min_r * cs + pad;
        int w = cs - pad * 2;
        int full_h = (max_r - min_r + 1) * cs - pad * 2;

        // Try vertical sprite
        if (draw_vcap_sprite(x, y, w, full_h, top_color, bottom_color))
            return;

        // Fallback: two halves
        int half_h = full_h / 2;
        if (sprites_) {
            draw_single_cap_sprite(x, y, w, half_h, top_color);
            draw_single_cap_sprite(x, y + half_h, w, half_h, bottom_color);
        } else {
            draw_pill_cell(x, y, w, half_h, top_color,
                           true, false, true, true);
            draw_pill_cell(x, y + half_h, w, half_h, bottom_color,
                           false, true, true, true);
        }
    }
}

// ====================== STAMPED PIECE ======================

void UIRenderer::draw_stamped_piece(int r, int c, int color, int bx, int by,
                                    int cs) {
    int pad = 2;
    int x = bx + c * cs + pad;
    int y = by + r * cs + pad;
    int w = cs - pad * 2;
    int h = cs - pad * 2;

    if (sprites_) {
        draw_single_cap_sprite(x, y, w, h, color);
    } else {
        draw_pill_cell(x, y, w, h, color, true, true, true, true);
    }
}

// ====================== DRAW CONNECTED HORIZONTAL PAIR ON BOARD ======================

void UIRenderer::draw_connected_hcap(int r, int c_left, int c_right,
                                     int color_left, int color_right,
                                     int bx, int by, int cs) {
    int pad = 2;
    int x = bx + c_left * cs + pad;
    int y = by + r * cs + pad;
    int w = (c_right - c_left + 1) * cs - pad * 2;
    int h = cs - pad * 2;

    if (draw_hcap_sprite(x, y, w, h, color_left, color_right))
        return;

    // Fallback: draw two single cells
    int half_w = w / 2;
    if (sprites_) {
        draw_single_cap_sprite(x, y, half_w, h, color_left);
        draw_single_cap_sprite(x + half_w, y, half_w, h, color_right);
    } else {
        draw_pill_cell(x, y, half_w, h, color_left, true, true, true, false);
        draw_pill_cell(x + half_w, y, half_w, h, color_right, true, true,
                       false, true);
    }
}

// ====================== DRAW CONNECTED VERTICAL PAIR ON BOARD ======================

void UIRenderer::draw_connected_vcap(int r_top, int r_bottom, int c,
                                     int top_color, int bottom_color,
                                     int bx, int by, int cs) {
    int pad = 2;
    int x = bx + c * cs + pad;
    int y = by + r_top * cs + pad;
    int w = cs - pad * 2;
    int h = (r_bottom - r_top + 1) * cs - pad * 2;

    if (draw_vcap_sprite(x, y, w, h, top_color, bottom_color))
        return;

    // Fallback: draw two single cells
    int half_h = h / 2;
    if (sprites_) {
        draw_single_cap_sprite(x, y, w, half_h, top_color);
        draw_single_cap_sprite(x, y + half_h, w, half_h, bottom_color);
    } else {
        draw_pill_cell(x, y, w, half_h, top_color, true, false, true, true);
        draw_pill_cell(x, y + half_h, w, half_h, bottom_color, false, true,
                       true, true);
    }
}

// ====================== BOARD ======================

void UIRenderer::draw_board(const PlayerBoard &board, int bx, int by,
                            int cell_size, float time) {
    int bw = COLS * cell_size;
    int bh = ROWS * cell_size;
    int frame = 4;

    // Board frame
    gfx_.draw_rect(bx - frame, by - frame, bw + frame * 2, bh + frame * 2,
                   Theme::BORDER_BOARD);

    // Board background
    gfx_.draw_rect(bx, by, bw, bh, Theme::BG_BOARD);

    // Grid lines
    for (int r = 0; r <= ROWS; r++) {
        int y = by + r * cell_size;
        gfx_.draw_line(bx, y, bx + bw, y, Theme::GRID_LINE);
    }
    for (int c = 0; c <= COLS; c++) {
        int x = bx + c * cell_size;
        gfx_.draw_line(x, by, x, by + bh, Theme::GRID_LINE);
    }

    // Draw cells — scan for connected pairs (horizontal and vertical)
    // We track drawn cells to avoid double-drawing pairs.
    bool drawn[ROWS][COLS] = {};

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            if (drawn[r][c])
                continue;
            const Piece &p = board.grid[r][c];
            if (p.color == EMPTY) {
                continue;
            }

            if (p.virus) {
                int cx = bx + c * cell_size + cell_size / 2;
                int cy = by + r * cell_size + cell_size / 2;
                float vr = (float)(cell_size / 2 - 2);
                float phase = (float)(r * COLS + c) * 1.7f;
                draw_virus_sprite(cx, cy, vr, p.color, time, phase);
                drawn[r][c] = true;
                continue;
            }

            // Check for horizontal connection (same capId to the right)
            if (c + 1 < COLS) {
                const Piece &neighbor = board.grid[r][c + 1];
                if (!neighbor.virus && neighbor.color != EMPTY &&
                    neighbor.capId == p.capId && p.capId != 0) {
                    draw_connected_hcap(r, c, c + 1, p.color, neighbor.color,
                                        bx, by, cell_size);
                    drawn[r][c] = true;
                    drawn[r][c + 1] = true;
                    continue;
                }
            }

            // Check for vertical connection (same capId below)
            if (r + 1 < ROWS) {
                const Piece &neighbor = board.grid[r + 1][c];
                if (!neighbor.virus && neighbor.color != EMPTY &&
                    neighbor.capId == p.capId && p.capId != 0) {
                    draw_connected_vcap(r, r + 1, c, p.color, neighbor.color,
                                        bx, by, cell_size);
                    drawn[r][c] = true;
                    drawn[r + 1][c] = true;
                    continue;
                }
            }

            // Standalone cell
            draw_stamped_piece(r, c, p.color, bx, by, cell_size);
            drawn[r][c] = true;
        }
    }

    // Active capsule overlay (only during PLAYING phase)
    if (board.phase == Phase::PLAYING) {
        draw_active_capsule(board.cap, bx, by, cell_size);
    }
}

// ====================== SIDE PANEL ======================

void UIRenderer::draw_side_panel(const char *label, const PlayerBoard &board,
                                 int px, int py, int panel_w, int cell_size,
                                 float time) {
    int frame = 3;
    int content_w = panel_w - frame * 2;
    int content_x = px + frame;
    int panel_h = ROWS * cell_size;

    // Panel frame + background
    gfx_.draw_rect(px, py, panel_w, panel_h, Theme::BORDER_PANEL);
    gfx_.draw_rect(px + frame, py + frame, content_w, panel_h - frame * 2,
                   Theme::BG_PANEL);

    int y = py + frame + 10;

    // (Label removed)

    // "NEXT" label
    const char *next_label = "NEXT";
    int nlw = gfx_.text_width(next_label, 18);
    gfx_.draw_text(content_x + (content_w - nlw) / 2, y, next_label, 18,
                   Theme::TEXT_SECONDARY);
    y += 24;

    // NEXT preview box
    int preview_cell = cell_size * 2 / 3;
    int preview_w = preview_cell * 2 + 8;
    int preview_h = preview_cell + 8;
    int preview_x = content_x + (content_w - preview_w) / 2;

    gfx_.draw_rect(preview_x - 1, y - 1, preview_w + 2, preview_h + 2,
                   Theme::BORDER_NEXT);
    gfx_.draw_rect(preview_x, y, preview_w, preview_h, Theme::BG_BOARD);

    // Draw next capsule using sprites or primitive fallback
    int hx = preview_x + 4;
    int hy = y + 4;
    int full_w = preview_cell * 2;

    if (sprites_) {
        // Try the horizontal capsule sprite
        if (!draw_hcap_sprite(hx, hy, full_w, preview_cell,
                              board.nxt.h1, board.nxt.h2)) {
            // Fallback: two single sprites
            draw_single_cap_sprite(hx, hy, preview_cell, preview_cell,
                                   board.nxt.h1);
            draw_single_cap_sprite(hx + preview_cell, hy,
                                   preview_cell, preview_cell, board.nxt.h2);
        }
    } else {
        draw_pill_cell(hx, hy, preview_cell, preview_cell, board.nxt.h1,
                       true, true, true, false);
        draw_pill_cell(hx + preview_cell, hy, preview_cell, preview_cell,
                       board.nxt.h2, true, true, false, true);
    }
    y += preview_h + 16;

    // Divider
    gfx_.draw_line(content_x, y, content_x + content_w, y,
                   Theme::BORDER_PANEL);
    y += 10;

    // Virus count
    int viruses_left = board.total_viruses - board.cleared_viruses;
    const char *v_label = "VIRUSES";
    int vlw = gfx_.text_width(v_label, 18);
    gfx_.draw_text(content_x + (content_w - vlw) / 2, y, v_label, 18,
                   Theme::TEXT_SECONDARY);
    y += 22;

    UIColor v_clr = (viruses_left > 5) ? Theme::WIN_GREEN : Theme::LOSE_RED;
    std::string v_str = std::to_string(viruses_left);
    int vsw = gfx_.text_width(v_str.c_str(), 28);
    gfx_.draw_text(content_x + (content_w - vsw) / 2, y, v_str.c_str(), 28,
                   v_clr);
    y += 36;

}

// ====================== CENTER HUD ======================

void UIRenderer::draw_center_hud(int wins, int losses, int player_viruses,
                                 int bot_viruses, int player_attacks,
                                 int bot_attacks, int round_num,
                                 const LayoutMetrics &layout) {
    int cx = layout.center_x;
    int cw = layout.center_w;

    // --- Winners grid ---
    int grid_cell = (int)(layout.cell_size * 1.3f);
    int grid_cols = 2;
    int grid_rows = MATCH_WINS;
    int grid_w = grid_cols * grid_cell;
    int grid_h = grid_rows * grid_cell;
    int gx = cx + (cw - grid_w) / 2;
    int gpad = 3;

    // Position grid so its bottom (+ padding) aligns with board bottom
    int board_bottom = layout.board_y + layout.board_h;
    int gy = board_bottom - grid_h - 4;

    // --- Round number (just above the grid) ---
    std::string round_str = fmt("ROUND %d", round_num);
    int rsw = gfx_.text_width(round_str.c_str(), 28);
    gfx_.draw_text(cx + (cw - rsw) / 2, gy - 40, round_str.c_str(), 28,
                   Theme::TEXT_PRIMARY);

    gfx_.draw_rect(gx - 4, gy - 4, grid_w + 8, grid_h + 8, Theme::BG_PANEL);
    gfx_.draw_rect_outline(gx - 4, gy - 4, grid_w + 8, grid_h + 8,
                           Theme::BORDER_PANEL);

    for (int r = 0; r < grid_rows; r++) {
        for (int c = 0; c < grid_cols; c++) {
            int cell_x = gx + c * grid_cell + gpad;
            int cell_y = gy + r * grid_cell + gpad;
            int cell_w = grid_cell - gpad * 2;
            int cell_h = grid_cell - gpad * 2;

            int row_from_bottom = grid_rows - 1 - r;
            UIColor fill = Theme::BG_CELL_EMPTY;

            if (c == 0 && row_from_bottom < wins)
                fill = Theme::GOLD;
            else if (c == 1 && row_from_bottom < losses)
                fill = Theme::LOSE_RED;

            gfx_.draw_rect(cell_x, cell_y, cell_w, cell_h, fill);
        }
    }

    int ay = board_bottom + 10;

    // --- Attack indicators ---
    if (player_attacks > 0) {
        std::string atk_str = fmt(">> %d ATTACK >>", player_attacks);
        int asw = gfx_.text_width(atk_str.c_str(), 20);
        gfx_.draw_text(cx + (cw - asw) / 2, ay, atk_str.c_str(), 20,
                       Theme::ATTACK_RED);
        ay += 28;
    }
    if (bot_attacks > 0) {
        std::string atk_str = fmt("<< %d ATTACK <<", bot_attacks);
        int asw = gfx_.text_width(atk_str.c_str(), 20);
        gfx_.draw_text(cx + (cw - asw) / 2, ay, atk_str.c_str(), 20,
                       Theme::ATTACK_RED);
        ay += 28;
    }
}

// ====================== GAME SCREEN (COMPOSITE) ======================

void UIRenderer::draw_game_screen(const PlayerBoard &player,
                                  const PlayerBoard &bot, int player_attacks,
                                  int bot_attacks, int wins, int losses,
                                  int round_num, const LayoutMetrics &layout,
                                  float time) {
    draw_side_panel("PLAYER", player, layout.left_panel_x, layout.board_y,
                    layout.panel_w, layout.cell_size, time);
    draw_side_panel("BOT", bot, layout.right_panel_x, layout.board_y,
                    layout.panel_w, layout.cell_size, time);

    draw_board(player, layout.left_board_x, layout.board_y, layout.cell_size,
               time);
    draw_board(bot, layout.right_board_x, layout.board_y, layout.cell_size,
               time);

    int p_viruses = player.total_viruses - player.cleared_viruses;
    int b_viruses = bot.total_viruses - bot.cleared_viruses;
    draw_center_hud(wins, losses, p_viruses, b_viruses, player_attacks,
                    bot_attacks, round_num, layout);
}

// ====================== TITLE SCREEN ======================

void UIRenderer::draw_title_screen(int selected,
                                   const std::vector<DecorVirus> &viruses,
                                   float time, int sw, int sh) {
    for (const auto &v : viruses) {
        float bob = sinf(time * 1.5f + v.phase) * 5.0f;
        draw_virus_sprite((int)v.x, (int)(v.y + bob), v.size, v.color, time,
                          v.phase);
    }

    const char *title = "DR. MARIO";
    int title_size = 64;
    int tw = gfx_.text_width(title, title_size);
    gfx_.draw_text(sw / 2 - tw / 2, 100, title, title_size, Theme::TEXT_TITLE);

    const char *sub = "- RETRO EDITION -";
    int sub_size = 22;
    int sw2 = gfx_.text_width(sub, sub_size);
    gfx_.draw_text(sw / 2 - sw2 / 2, 170, sub, sub_size, Theme::TEXT_DIM);

    struct Option {
        const char *name;
        const char *desc;
    };
    Option opts[] = {
        {"JANE MODE", "Virus: 3 | Speed: Slow | Bot: Kid"},
        {"DAD MODE", "Virus: 40 | Speed: Medium | Bot: Swift"},
        {"TEST MODE", "Virus: 1 | Speed: Very Slow | Bot: Kid"},
    };

    int opt_font = 32;
    int desc_font = 20;
    int opt_h = 90;
    int bar_w = 560;
    int bar_h = 75;
    int bar_x = sw / 2 - bar_w / 2;
    int total_h = 3 * opt_h;
    int start_y = sh / 2 - total_h / 2 + 40;

    UIColor bg_normal{0, 0, 0, 220};
    UIColor bg_selected{40, 40, 40, 250};
    UIColor border_normal{120, 120, 120, 220};
    UIColor border_selected{Theme::GOLD.r, Theme::GOLD.g, Theme::GOLD.b, 255};

    for (int i = 0; i < 3; i++) {
        int y = start_y + i * opt_h;
        bool sel = (i == selected);

        gfx_.draw_rect(bar_x, y - 4, bar_w, bar_h,
                        sel ? bg_selected : bg_normal);
        gfx_.draw_rect_outline(bar_x, y - 4, bar_w, bar_h,
                               sel ? border_selected : border_normal);

        UIColor name_clr = sel ? Theme::GOLD : Theme::TEXT_PRIMARY;
        int nw = gfx_.text_width(opts[i].name, opt_font);
        gfx_.draw_text(sw / 2 - nw / 2, y, opts[i].name, opt_font, name_clr);

        UIColor desc_clr = sel ? Theme::TEXT_PRIMARY : Theme::TEXT_SECONDARY;
        int dw = gfx_.text_width(opts[i].desc, desc_font);
        gfx_.draw_text(sw / 2 - dw / 2, y + 38, opts[i].desc, desc_font,
                       desc_clr);
    }

    const char *instr = "Up/Down to select | Enter to confirm | ESC to quit";
    int instr_size = 18;
    int iw = gfx_.text_width(instr, instr_size);
    gfx_.draw_text(sw / 2 - iw / 2, sh - 80, instr, instr_size,
                   Theme::TEXT_DIM);
}

// ====================== ROUND END OVERLAY ======================

void UIRenderer::draw_round_end(bool player_won,
                                const LayoutMetrics &layout) {
    // Draw a small overlay card centered on each board
    auto draw_card = [&](int board_x, int board_y, int board_w, int board_h,
                        const char *msg, UIColor clr) {
        // Semi-transparent overlay on the board
        gfx_.draw_rect(board_x, board_y, board_w, board_h, Theme::BG_OVERLAY);

        // Card sized to fit the text
        int fnt = 48;
        int tw = gfx_.text_width(msg, fnt);
        int card_w = tw + 60;
        int card_h = fnt + 40;
        int card_x = board_x + (board_w - card_w) / 2;
        int card_y = board_y + (board_h - card_h) / 2;

        gfx_.draw_rect(card_x, card_y, card_w, card_h, Theme::BG_PANEL);
        gfx_.draw_rect_outline(card_x, card_y, card_w, card_h,
                               Theme::BORDER_PANEL);

        gfx_.draw_text(card_x + (card_w - tw) / 2, card_y + (card_h - fnt) / 2,
                       msg, fnt, clr);
    };

    int bw = layout.board_w;
    int bh = layout.board_h;
    int frame = 4; // matches draw_board frame

    if (player_won) {
        draw_card(layout.left_board_x - frame, layout.board_y - frame,
                  bw + frame * 2, bh + frame * 2,
                  "ROUND WIN!", Theme::WIN_GREEN);
        draw_card(layout.right_board_x - frame, layout.board_y - frame,
                  bw + frame * 2, bh + frame * 2,
                  "ROUND LOST!", Theme::LOSE_RED);
    } else {
        draw_card(layout.left_board_x - frame, layout.board_y - frame,
                  bw + frame * 2, bh + frame * 2,
                  "ROUND LOST!", Theme::LOSE_RED);
        draw_card(layout.right_board_x - frame, layout.board_y - frame,
                  bw + frame * 2, bh + frame * 2,
                  "ROUND WIN!", Theme::WIN_GREEN);
    }
}

// ====================== BOT BATTLE HUD ======================

void UIRenderer::draw_bot_battle_hud(const std::string &bot1_name,
                                     const std::string &bot2_name, int wins1,
                                     int wins2, int trial, int total_trials,
                                     const LayoutMetrics &layout) {
    int cx = layout.center_x;
    int cw = layout.center_w;
    int y = layout.board_y + 5;

    gfx_.draw_text(layout.left_board_x, layout.board_y - 35, bot1_name.c_str(),
                   24, Theme::WIN_GREEN);
    gfx_.draw_text(layout.right_board_x, layout.board_y - 35,
                   bot2_name.c_str(), 24, Theme::LOSE_RED);

    std::string trial_str = fmt("Trial %d/%d", trial + 1, total_trials);
    int tsw = gfx_.text_width(trial_str.c_str(), 24);
    gfx_.draw_text(cx + (cw - tsw) / 2, y, trial_str.c_str(), 24,
                   Theme::TEXT_PRIMARY);
    y += 40;

    std::string s1 = fmt("%s: %d", bot1_name.c_str(), wins1);
    std::string s2 = fmt("%s: %d", bot2_name.c_str(), wins2);
    int s1w = gfx_.text_width(s1.c_str(), 22);
    gfx_.draw_text(cx + (cw - s1w) / 2, y, s1.c_str(), 22, Theme::WIN_GREEN);
    y += 30;
    int s2w = gfx_.text_width(s2.c_str(), 22);
    gfx_.draw_text(cx + (cw - s2w) / 2, y, s2.c_str(), 22, Theme::LOSE_RED);
}

// ====================== BOT BATTLE RESULT ======================

void UIRenderer::draw_bot_battle_result(const std::string &bot1_name,
                                        const std::string &bot2_name,
                                        int wins1, int wins2, int sw,
                                        int sh) {
    gfx_.draw_rect(0, 0, sw, sh, Theme::BG_OVERLAY);

    int card_w = 560;
    int card_h = 320;
    int card_x = sw / 2 - card_w / 2;
    int card_y = sh / 2 - card_h / 2;

    gfx_.draw_rect(card_x, card_y, card_w, card_h, Theme::BG_PANEL);
    gfx_.draw_rect_outline(card_x, card_y, card_w, card_h,
                           Theme::BORDER_PANEL);

    int cx = sw / 2;
    int y = card_y + 20;

    const char *title = "TOURNAMENT RESULTS";
    int tf = 40;
    int tw2 = gfx_.text_width(title, tf);
    gfx_.draw_text(cx - tw2 / 2, y, title, tf, Theme::TEXT_TITLE);
    y += 60;

    std::string s1 = fmt("%s: %d wins", bot1_name.c_str(), wins1);
    std::string s2 = fmt("%s: %d wins", bot2_name.c_str(), wins2);
    gfx_.draw_text(cx - 180, y, s1.c_str(), 28, Theme::WIN_GREEN);
    y += 38;
    gfx_.draw_text(cx - 180, y, s2.c_str(), 28, Theme::LOSE_RED);
    y += 55;

    std::string win_str;
    UIColor win_clr;
    if (wins1 > wins2) {
        win_str = fmt("%s wins!", bot1_name.c_str());
        win_clr = Theme::WIN_GREEN;
    } else if (wins2 > wins1) {
        win_str = fmt("%s wins!", bot2_name.c_str());
        win_clr = Theme::LOSE_RED;
    } else {
        win_str = "It's a tie!";
        win_clr = Theme::GOLD;
    }

    int wf = 40;
    int ww = gfx_.text_width(win_str.c_str(), wf);
    gfx_.draw_text(cx - ww / 2, y, win_str.c_str(), wf, win_clr);
    y += 55;

    const char *instr = "Any key: title | ESC: quit";
    int ifnt = 20;
    int iw = gfx_.text_width(instr, ifnt);
    gfx_.draw_text(cx - iw / 2, y, instr, ifnt, Theme::TEXT_SECONDARY);
}
