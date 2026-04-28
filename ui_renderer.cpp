#include "ui_renderer.h"
#include "ui_theme.h"
#include "constants.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

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

    // Right board + side panel (symmetric)
    m.right_board_x = sw - margin - m.panel_w - gap - m.board_w;
    m.right_panel_x = m.right_board_x + m.board_w + gap;

    // Center HUD fills the gap between boards
    m.center_x = m.left_board_x + m.board_w;
    m.center_w = m.right_board_x - m.center_x;

    return m;
}

// ====================== CONSTRUCTOR ======================

UIRenderer::UIRenderer(Gfx &gfx) : gfx_(gfx) {}

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
    UIColor body = Theme::virus_body(color);
    UIColor legs = Theme::virus_legs(color);

    // Animate legs
    float wiggle = sinf(time * 3.0f + phase) * 0.35f;
    float leg_len = radius * 0.5f;

    // 4 diagonal legs
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

    // Body (filled circle)
    int body_r = (int)(radius * 0.62f);
    gfx_.draw_circle(cx, cy, body_r, body);

    // Pixel-art eyes
    int eye_size = std::max(2, (int)(body_r * 0.35f));
    int pupil_size = std::max(1, eye_size / 2);
    int eye_spacing = (int)(body_r * 0.45f);
    int eye_y = cy - (int)(body_r * 0.15f);

    // Left eye
    int lex = cx - eye_spacing - eye_size / 2;
    int ley = eye_y - eye_size / 2;
    gfx_.draw_rect(lex, ley, eye_size, eye_size, Theme::EYE_WHITE);
    gfx_.draw_rect(lex + pupil_size, ley + pupil_size / 2, pupil_size,
                   pupil_size, Theme::EYE_PUPIL);

    // Right eye
    int rex = cx + eye_spacing - eye_size / 2;
    gfx_.draw_rect(rex, ley, eye_size, eye_size, Theme::EYE_WHITE);
    gfx_.draw_rect(rex + pupil_size, ley + pupil_size / 2, pupil_size,
                   pupil_size, Theme::EYE_PUPIL);
}

// ====================== PILL CELL (BEVELED) ======================

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
        int half_w = full_w / 2;

        // Left half
        draw_pill_cell(x, y, half_w, h, left_color,
                       /*top=*/true, /*bottom=*/true,
                       /*left=*/true, /*right=*/false);
        // Right half
        draw_pill_cell(x + half_w, y, half_w, h, right_color,
                       /*top=*/true, /*bottom=*/true,
                       /*left=*/false, /*right=*/true);
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
        int half_h = full_h / 2;

        // Top half
        draw_pill_cell(x, y, w, half_h, top_color,
                       /*top=*/true, /*bottom=*/false,
                       /*left=*/true, /*right=*/true);
        // Bottom half
        draw_pill_cell(x, y + half_h, w, half_h, bottom_color,
                       /*top=*/false, /*bottom=*/true,
                       /*left=*/true, /*right=*/true);
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
    draw_pill_cell(x, y, w, h, color, true, true, true, true);
}

// ====================== BOARD ======================

void UIRenderer::draw_board(const PlayerBoard &board, int bx, int by,
                            int cell_size, float time) {
    int bw = COLS * cell_size;
    int bh = ROWS * cell_size;
    int frame = 4;

    // Board frame (draw behind)
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

    // Draw cells
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            const Piece &p = board.grid[r][c];
            if (p.color == EMPTY)
                continue;

            int cx = bx + c * cell_size + cell_size / 2;
            int cy = by + r * cell_size + cell_size / 2;

            if (p.virus) {
                float vr = (float)(cell_size / 2 - 2);
                float phase = (float)(r * COLS + c) * 1.7f;
                draw_virus_sprite(cx, cy, vr, p.color, time, phase);
            } else {
                draw_stamped_piece(r, c, p.color, bx, by, cell_size);
            }
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

    // Label (PLAYER / BOT)
    int label_w = gfx_.text_width(label, 24);
    gfx_.draw_text(content_x + (content_w - label_w) / 2, y, label, 24,
                   Theme::TEXT_PRIMARY);
    y += 36;

    // Divider line
    gfx_.draw_line(content_x, y, content_x + content_w, y,
                   Theme::BORDER_PANEL);
    y += 10;

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

    // Two halves side by side
    int hx = preview_x + 4;
    int hy = y + 4;
    draw_pill_cell(hx, hy, preview_cell, preview_cell, board.nxt.h1, true,
                   true, true, false);
    draw_pill_cell(hx + preview_cell, hy, preview_cell, preview_cell,
                   board.nxt.h2, true, true, false, true);
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

    // Divider
    gfx_.draw_line(content_x, y, content_x + content_w, y,
                   Theme::BORDER_PANEL);
    y += 10;

    // Score
    const char *s_label = "SCORE";
    int slw = gfx_.text_width(s_label, 18);
    gfx_.draw_text(content_x + (content_w - slw) / 2, y, s_label, 18,
                   Theme::TEXT_SECONDARY);
    y += 22;

    std::string s_str = std::to_string(board.score);
    int ssw = gfx_.text_width(s_str.c_str(), 24);
    gfx_.draw_text(content_x + (content_w - ssw) / 2, y, s_str.c_str(), 24,
                   Theme::GOLD);
}

// ====================== CENTER HUD ======================

void UIRenderer::draw_center_hud(int wins, int losses, int player_viruses,
                                 int bot_viruses, int player_attacks,
                                 int bot_attacks, int round_num,
                                 const LayoutMetrics &layout) {
    int cx = layout.center_x;
    int cw = layout.center_w;
    int cy = layout.board_y;

    // --- Round number ---
    std::string round_str = fmt("ROUND %d", round_num);
    int rsw = gfx_.text_width(round_str.c_str(), 28);
    gfx_.draw_text(cx + (cw - rsw) / 2, cy + 5, round_str.c_str(), 28,
                   Theme::TEXT_PRIMARY);
    cy += 45;

    // --- Winners grid ---
    const char *wlabel = "Winners";
    int wlw = gfx_.text_width(wlabel, 22);
    gfx_.draw_text(cx + (cw - wlw) / 2, cy, wlabel, 22, Theme::TEXT_PRIMARY);
    cy += 28;

    int grid_cell = (int)(layout.cell_size * 1.3f);
    int grid_cols = 2;
    int grid_rows = MATCH_WINS;
    int grid_w = grid_cols * grid_cell;
    int grid_h = grid_rows * grid_cell;
    int gx = cx + (cw - grid_w) / 2;
    int gy = cy;
    int gpad = 3;

    // Grid background
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

    // Column labels
    int label_y = gy + grid_h + 8;
    int plw = gfx_.text_width("YOU", 18);
    gfx_.draw_text(gx + (grid_cell - plw) / 2, label_y, "YOU", 18,
                   Theme::WIN_GREEN);
    int blw = gfx_.text_width("BOT", 18);
    gfx_.draw_text(gx + grid_cell + (grid_cell - blw) / 2, label_y, "BOT", 18,
                   Theme::LOSE_RED);

    cy = gy + grid_h + 40;

    // --- Record ---
    std::string rec_str = fmt("%d - %d", wins, losses);
    int recw = gfx_.text_width(rec_str.c_str(), 24);
    gfx_.draw_text(cx + (cw - recw) / 2, cy, rec_str.c_str(), 24,
                   Theme::TEXT_PRIMARY);
    cy += 35;

    // --- Win rate bar ---
    int total = wins + losses;
    if (total > 0) {
        int bar_w = cw - 40;
        int bar_h = 16;
        int bar_x = cx + 20;
        int win_w = (wins * bar_w) / total;

        gfx_.draw_rect(bar_x, cy, win_w, bar_h, Theme::WIN_GREEN);
        gfx_.draw_rect(bar_x + win_w, cy, bar_w - win_w, bar_h,
                       Theme::LOSE_RED);
        gfx_.draw_rect_outline(bar_x, cy, bar_w, bar_h, Theme::BORDER_PANEL);

        int pct = (wins * 100) / total;
        std::string pct_str = fmt("%d%%", pct);
        gfx_.draw_text(bar_x + bar_w + 8, cy - 2, pct_str.c_str(), 20,
                       Theme::GOLD);
        cy += 30;
    }

    cy += 15;

    // --- Attack indicators ---
    if (player_attacks > 0) {
        std::string atk_str = fmt(">> %d ATTACK >>", player_attacks);
        int asw = gfx_.text_width(atk_str.c_str(), 20);
        gfx_.draw_text(cx + (cw - asw) / 2, cy, atk_str.c_str(), 20,
                       Theme::ATTACK_RED);
        cy += 28;
    }
    if (bot_attacks > 0) {
        std::string atk_str = fmt("<< %d ATTACK <<", bot_attacks);
        int asw = gfx_.text_width(atk_str.c_str(), 20);
        gfx_.draw_text(cx + (cw - asw) / 2, cy, atk_str.c_str(), 20,
                       Theme::ATTACK_RED);
        cy += 28;
    }
}

// ====================== GAME SCREEN (COMPOSITE) ======================

void UIRenderer::draw_game_screen(const PlayerBoard &player,
                                  const PlayerBoard &bot, int player_attacks,
                                  int bot_attacks, int wins, int losses,
                                  int round_num, const LayoutMetrics &layout,
                                  float time) {
    // Side panels
    draw_side_panel("PLAYER", player, layout.left_panel_x, layout.board_y,
                    layout.panel_w, layout.cell_size, time);
    draw_side_panel("BOT", bot, layout.right_panel_x, layout.board_y,
                    layout.panel_w, layout.cell_size, time);

    // Boards
    draw_board(player, layout.left_board_x, layout.board_y, layout.cell_size,
               time);
    draw_board(bot, layout.right_board_x, layout.board_y, layout.cell_size,
               time);

    // Center HUD
    int p_viruses = player.total_viruses - player.cleared_viruses;
    int b_viruses = bot.total_viruses - bot.cleared_viruses;
    draw_center_hud(wins, losses, p_viruses, b_viruses, player_attacks,
                    bot_attacks, round_num, layout);
}

// ====================== TITLE SCREEN ======================

void UIRenderer::draw_title_screen(int selected,
                                   const std::vector<DecorVirus> &viruses,
                                   float time, int sw, int sh) {
    // Background viruses
    for (const auto &v : viruses) {
        float bob = sinf(time * 1.5f + v.phase) * 5.0f;
        draw_virus_sprite((int)v.x, (int)(v.y + bob), v.size, v.color, time,
                          v.phase);
    }

    // Title
    const char *title = "DR. MARIO";
    int title_size = 64;
    int tw = gfx_.text_width(title, title_size);
    gfx_.draw_text(sw / 2 - tw / 2, 100, title, title_size, Theme::TEXT_TITLE);

    // Subtitle line
    const char *sub = "- RETRO EDITION -";
    int sub_size = 22;
    int sw2 = gfx_.text_width(sub, sub_size);
    gfx_.draw_text(sw / 2 - sw2 / 2, 170, sub, sub_size, Theme::TEXT_DIM);

    // Mode options
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
    int total_h = 3 * opt_h;
    int start_y = sh / 2 - total_h / 2 + 40;

    for (int i = 0; i < 3; i++) {
        int y = start_y + i * opt_h;

        // Selection highlight bar
        if (i == selected) {
            int bar_w = 560;
            int bar_h = 75;
            int bar_x = sw / 2 - bar_w / 2;
            gfx_.draw_rect(bar_x, y - 4, bar_w, bar_h, Theme::BG_HIGHLIGHT);
            gfx_.draw_rect_outline(bar_x, y - 4, bar_w, bar_h,
                                   Theme::BORDER_PANEL);
        }

        // Option name
        UIColor name_clr =
            (i == selected) ? Theme::GOLD : Theme::TEXT_SECONDARY;
        int nw = gfx_.text_width(opts[i].name, opt_font);
        gfx_.draw_text(sw / 2 - nw / 2, y, opts[i].name, opt_font, name_clr);

        // Description
        int dw = gfx_.text_width(opts[i].desc, desc_font);
        gfx_.draw_text(sw / 2 - dw / 2, y + 38, opts[i].desc, desc_font,
                       Theme::TEXT_DIM);
    }

    // Controls hint
    const char *instr = "Up/Down to select | Enter to confirm | ESC to quit";
    int instr_size = 18;
    int iw = gfx_.text_width(instr, instr_size);
    gfx_.draw_text(sw / 2 - iw / 2, sh - 80, instr, instr_size,
                   Theme::TEXT_DIM);
}

// ====================== ROUND END OVERLAY ======================

void UIRenderer::draw_round_end(bool player_won, int wins, int losses,
                                int round_num, int sw, int sh) {
    // Dark overlay
    gfx_.draw_rect(0, 0, sw, sh, Theme::BG_OVERLAY);

    // Panel card
    int card_w = 500;
    int card_h = 340;
    int card_x = sw / 2 - card_w / 2;
    int card_y = sh / 2 - card_h / 2;

    gfx_.draw_rect(card_x, card_y, card_w, card_h, Theme::BG_PANEL);
    gfx_.draw_rect_outline(card_x, card_y, card_w, card_h,
                           Theme::BORDER_PANEL);

    int cx = sw / 2;
    int y = card_y + 25;

    // Result text
    if (player_won) {
        const char *msg = "ROUND WIN!";
        int fnt = 52;
        int w = gfx_.text_width(msg, fnt);
        gfx_.draw_text(cx - w / 2, y, msg, fnt, Theme::WIN_GREEN);
    } else {
        const char *msg = "ROUND LOSS";
        int fnt = 52;
        int w = gfx_.text_width(msg, fnt);
        gfx_.draw_text(cx - w / 2, y, msg, fnt, Theme::LOSE_RED);
    }
    y += 65;

    // Round info
    std::string rnd = fmt("Round %d", round_num);
    int rw = gfx_.text_width(rnd.c_str(), 28);
    gfx_.draw_text(cx - rw / 2, y, rnd.c_str(), 28, Theme::TEXT_PRIMARY);
    y += 40;

    // W/L
    std::string w_str = fmt("Wins: %d", wins);
    std::string l_str = fmt("Losses: %d", losses);
    gfx_.draw_text(cx - 120, y, w_str.c_str(), 26, Theme::WIN_GREEN);
    gfx_.draw_text(cx + 30, y, l_str.c_str(), 26, Theme::LOSE_RED);
    y += 40;

    // Win rate bar
    int total = wins + losses;
    if (total > 0) {
        int bar_w = 300;
        int bar_h = 18;
        int bar_x = cx - bar_w / 2;
        int win_w = (wins * bar_w) / total;

        gfx_.draw_rect(bar_x, y, win_w, bar_h, Theme::WIN_GREEN);
        gfx_.draw_rect(bar_x + win_w, y, bar_w - win_w, bar_h,
                       Theme::LOSE_RED);
        gfx_.draw_rect_outline(bar_x, y, bar_w, bar_h, Theme::BORDER_PANEL);

        int pct = (wins * 100) / total;
        std::string pct_str = fmt("%d%%", pct);
        gfx_.draw_text(bar_x + bar_w + 10, y - 1, pct_str.c_str(), 22,
                       Theme::GOLD);
        y += 35;
    }

    y += 10;

    // Instructions
    const char *instr = "Any key: continue | ESC: quit";
    int ifnt = 20;
    int iw = gfx_.text_width(instr, ifnt);
    gfx_.draw_text(cx - iw / 2, y, instr, ifnt, Theme::TEXT_SECONDARY);
}

// ====================== BOT BATTLE HUD ======================

void UIRenderer::draw_bot_battle_hud(const std::string &bot1_name,
                                     const std::string &bot2_name, int wins1,
                                     int wins2, int trial, int total_trials,
                                     const LayoutMetrics &layout) {
    int cx = layout.center_x;
    int cw = layout.center_w;
    int y = layout.board_y + 5;

    // Bot names
    gfx_.draw_text(layout.left_board_x, layout.board_y - 35, bot1_name.c_str(),
                   24, Theme::WIN_GREEN);
    gfx_.draw_text(layout.right_board_x, layout.board_y - 35,
                   bot2_name.c_str(), 24, Theme::LOSE_RED);

    // Trial progress
    std::string trial_str = fmt("Trial %d/%d", trial + 1, total_trials);
    int tsw = gfx_.text_width(trial_str.c_str(), 24);
    gfx_.draw_text(cx + (cw - tsw) / 2, y, trial_str.c_str(), 24,
                   Theme::TEXT_PRIMARY);
    y += 40;

    // Scores
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
    // Dark overlay
    gfx_.draw_rect(0, 0, sw, sh, Theme::BG_OVERLAY);

    // Panel card
    int card_w = 560;
    int card_h = 320;
    int card_x = sw / 2 - card_w / 2;
    int card_y = sh / 2 - card_h / 2;

    gfx_.draw_rect(card_x, card_y, card_w, card_h, Theme::BG_PANEL);
    gfx_.draw_rect_outline(card_x, card_y, card_w, card_h,
                           Theme::BORDER_PANEL);

    int cx = sw / 2;
    int y = card_y + 20;

    // Title
    const char *title = "TOURNAMENT RESULTS";
    int tf = 40;
    int tw2 = gfx_.text_width(title, tf);
    gfx_.draw_text(cx - tw2 / 2, y, title, tf, Theme::TEXT_TITLE);
    y += 60;

    // Scores
    std::string s1 = fmt("%s: %d wins", bot1_name.c_str(), wins1);
    std::string s2 = fmt("%s: %d wins", bot2_name.c_str(), wins2);
    gfx_.draw_text(cx - 180, y, s1.c_str(), 28, Theme::WIN_GREEN);
    y += 38;
    gfx_.draw_text(cx - 180, y, s2.c_str(), 28, Theme::LOSE_RED);
    y += 55;

    // Winner
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

    // Instructions
    const char *instr = "Any key: title | ESC: quit";
    int ifnt = 20;
    int iw = gfx_.text_width(instr, ifnt);
    gfx_.draw_text(cx - iw / 2, y, instr, ifnt, Theme::TEXT_SECONDARY);
}
