#include "raylib.h"

// Undef raylib color macros that conflict with our game color constants
#undef RED
#undef YELLOW
#undef BLUE

#include "raylib_renderer.h"
#include "constants.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

// ====================== COLOR HELPERS ======================

Color pill_color(int c) {
  switch (c) {
  case RED:
    return {220, 50, 50, 255};
  case YELLOW:
    return {255, 220, 50, 255};
  case BLUE:
    return {50, 100, 220, 255};
  default:
    return {128, 128, 128, 255};
  }
}

Color pill_dark_color(int c) {
  switch (c) {
  case RED:
    return {160, 30, 30, 255};
  case YELLOW:
    return {180, 150, 30, 255};
  case BLUE:
    return {30, 70, 160, 255};
  default:
    return {80, 80, 80, 255};
  }
}

// ====================== DECORATIVE VIRUSES ======================

std::vector<DecorVirus> generate_decor_viruses(int count, int sw, int sh) {
  std::vector<DecorVirus> viruses;
  for (int i = 0; i < count; i++) {
    DecorVirus v;
    v.x = (float)(std::rand() % sw);
    v.y = (float)(std::rand() % sh);
    v.color = 1 + (std::rand() % 3); // RED, YELLOW, BLUE
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

  float board_left_pct = 0.10f;
  float board_right_pct = 0.10f;
  float board_vert_pct = 0.82f;

  m.cell_size = (int)(sh * board_vert_pct) / ROWS;
  m.board_w = COLS * m.cell_size;
  m.board_h = ROWS * m.cell_size;

  m.left_x = (int)(sw * board_left_pct);
  m.left_y = (sh - m.board_h) / 2;

  m.right_x = sw - (int)(sw * board_right_pct) - m.board_w;
  m.right_y = m.left_y;

  return m;
}

// ====================== BACKGROUND ======================

void draw_bg(Texture2D bg, int sw, int sh) {
  DrawTexturePro(bg, (Rectangle){0, 0, (float)bg.width, (float)bg.height},
                 (Rectangle){0, 0, (float)sw, (float)sh}, (Vector2){0, 0}, 0,
                 WHITE);
}

// ====================== VIRUS SPRITE ======================

void draw_virus_sprite(int cx, int cy, float radius, int color, float time,
                       float phase) {
  Color body = pill_color(color);
  Color legs = pill_dark_color(color);

  // Animate legs
  float wiggle = sinf(time * 3.0f + phase) * 0.35f;
  float leg_len = radius * 0.5f;

  // 4 diagonal legs
  for (int i = 0; i < 4; i++) {
    float base_angle = PI / 4.0f + (float)i * PI / 2.0f;
    float angle = base_angle + wiggle;
    float start_dist = radius * 0.6f;
    float sx = cx + cosf(angle) * start_dist;
    float sy = cy + sinf(angle) * start_dist;
    float ex = cx + cosf(angle) * (start_dist + leg_len);
    float ey = cy + sinf(angle) * (start_dist + leg_len);
    DrawLineEx((Vector2){sx, sy}, (Vector2){ex, ey}, 3.0f, legs);
  }

  // Body (filled circle)
  DrawCircle(cx, cy, (int)(radius * 0.62f), body);
}

// ====================== ACTIVE CAPSULE (PILL) ======================

static void draw_pill(const Capsule &cap, int bx, int by, int cell_size) {
  int pad = 2;

  auto draw_half = [&](int r, int c, int color) {
    DrawRectangle(bx + c * cell_size + pad, by + r * cell_size + pad,
                  cell_size - pad * 2, cell_size - pad * 2, pill_color(color));
  };

  draw_half(cap.r1(), cap.c1(), cap.h1);
  draw_half(cap.r2(), cap.c2(), cap.h2);
}

// ====================== BOARD ======================

void draw_board(const PlayerBoard &board, int bx, int by, int cell_size,
                float time) {
  int bw = COLS * cell_size;
  int bh = ROWS * cell_size;
  int pad = 2;

  // Board background (semi-transparent dark)
  DrawRectangleRounded((Rectangle){(float)(bx - 4), (float)(by - 4),
                                    (float)(bw + 8), (float)(bh + 8)},
                        0.03f, 4, (Color){0, 0, 0, 170});

  // Grid lines
  for (int r = 0; r <= ROWS; r++) {
    DrawLine(bx, by + r * cell_size, bx + bw, by + r * cell_size,
             (Color){255, 255, 255, 25});
  }
  for (int c = 0; c <= COLS; c++) {
    DrawLine(bx + c * cell_size, by, bx + c * cell_size, by + bh,
             (Color){255, 255, 255, 25});
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
        float vr = (float)(cell_size / 2 - pad);
        float phase = (float)(r * COLS + c) * 1.7f;
        draw_virus_sprite(cx, cy, vr, p.color, time, phase);
      } else {
        // Stamped capsule piece — rounded rectangle
        Color clr = pill_color(p.color);
        DrawRectangleRounded(
            (Rectangle){(float)(bx + c * cell_size + pad),
                        (float)(by + r * cell_size + pad),
                        (float)(cell_size - pad * 2),
                        (float)(cell_size - pad * 2)},
            0.3f, 6, clr);
      }
    }
  }

  // Active capsule overlay (only during PLAYING phase)
  if (board.phase == Phase::PLAYING) {
    draw_pill(board.cap, bx, by, cell_size);
  }

  // Board border
  DrawRectangleRoundedLines(
      (Rectangle){(float)(bx - 4), (float)(by - 4), (float)(bw + 8),
                  (float)(bh + 8)},
      0.03f, 4, (Color){255, 255, 255, 100});
}

// ====================== NEXT PIECE PREVIEW ======================

void draw_next_piece(const PlayerBoard &board, int bx, int by, int cell_size,
                     const char *label) {
  int preview_cell = cell_size * 2 / 3;
  int box_w = preview_cell * 3;
  int box_h = preview_cell * 2 + 30;

  // Label
  DrawText(label, bx, by - box_h - 25, 24, RAYWHITE);

  // Box background
  DrawRectangleRounded((Rectangle){(float)bx, (float)(by - box_h),
                                    (float)box_w, (float)box_h},
                        0.1f, 4, (Color){0, 0, 0, 150});

  // "NEXT" text
  DrawText("NEXT", bx + 8, by - box_h + 4, 18, GRAY);

  // Draw the two halves side by side
  int nx = bx + (box_w - preview_cell * 2) / 2;
  int ny = by - box_h + 26;
  int p = 2;

  DrawRectangle(nx + p, ny + p, preview_cell - p * 2, preview_cell - p * 2,
                pill_color(board.nxt.h1));
  DrawRectangle(nx + preview_cell + p, ny + p, preview_cell - p * 2,
                preview_cell - p * 2, pill_color(board.nxt.h2));
}

// ====================== TITLE SCREEN ======================

void draw_title_screen(int selected, const std::vector<DecorVirus> & /*viruses*/,
                       float time, int sw, int sh) {
  // Title
  const char *title = "DR. MARIO";
  int title_size = 64;
  int title_w = MeasureText(title, title_size);
  DrawText(title, sw / 2 - title_w / 2, 120, title_size, RAYWHITE);

  // Mode options
  struct Option {
    const char *name;
    const char *desc;
  };
  Option opts[] = {
      {"1.  Jane Mode", "Virus: 3  |  Speed: Slow  |  Bot: Kid"},
      {"2.  Dad Mode", "Virus: 40  |  Speed: Medium  |  Bot: Swift"},
      {"3.  Test Mode", "Virus: 1  |  Speed: Very Slow  |  Bot: Kid"},
  };

  int opt_font = 32;
  int desc_font = 22;
  int opt_h = 90; // total height per option
  int total_h = 3 * opt_h;
  int start_y = sh / 2 - total_h / 2 + 40;

  for (int i = 0; i < 3; i++) {
    int y = start_y + i * opt_h;

    // Highlight bar for selected option
    if (i == selected) {
      int bar_w = 560;
      int bar_h = 75;
      int bar_x = sw / 2 - bar_w / 2;
      DrawRectangle(bar_x, y - 4, bar_w, bar_h,
                    (Color){255, 255, 255, 70});
    }

    // Option name
    Color name_clr = (i == selected) ? RAYWHITE : (Color){180, 180, 180, 255};
    int name_w = MeasureText(opts[i].name, opt_font);
    DrawText(opts[i].name, sw / 2 - name_w / 2, y, opt_font, name_clr);

    // Description
    int desc_w = MeasureText(opts[i].desc, desc_font);
    DrawText(opts[i].desc, sw / 2 - desc_w / 2, y + 38, desc_font,
             (Color){140, 140, 140, 255});
  }

  // Instructions
  const char *instr = "Arrow keys to select, any key to confirm";
  int instr_font = 20;
  int instr_w = MeasureText(instr, instr_font);
  DrawText(instr, sw / 2 - instr_w / 2, sh - 100, instr_font,
           (Color){120, 120, 120, 255});
}

// ====================== GAME HUD ======================

void draw_game_hud(const PlayerBoard &player, const PlayerBoard &bot,
                   int player_attacks, int bot_attacks, int wins, int losses,
                   int round_num, const LayoutMetrics &layout, float time) {
  // Next piece previews above each board
  draw_next_piece(player, layout.left_x, layout.left_y, layout.cell_size,
                  "PLAYER");
  draw_next_piece(bot, layout.right_x, layout.right_y, layout.cell_size,
                  "BOT");

  // Center HUD between boards
  int center_x = (layout.left_x + layout.board_w + layout.right_x) / 2;
  int center_y = layout.left_y + 20;
  int font = 22;

  // Round
  DrawText(TextFormat("Round %d", round_num), center_x - 40, center_y, font,
           RAYWHITE);

  // Scores
  center_y += 40;
  DrawText(TextFormat("Score: %d", player.score), center_x - 50, center_y,
           font, (Color){150, 255, 150, 255});

  center_y += 30;
  DrawText(TextFormat("Score: %d", bot.score), center_x - 50, center_y, font,
           (Color){255, 150, 150, 255});

  // Virus counts
  center_y += 50;
  int p_rem = player.total_viruses - player.cleared_viruses;
  DrawText(TextFormat("Virus: %d/%d", p_rem, player.total_viruses),
           center_x - 50, center_y, font, (Color){150, 255, 150, 255});

  center_y += 30;
  int b_rem = bot.total_viruses - bot.cleared_viruses;
  DrawText(TextFormat("Virus: %d/%d", b_rem, bot.total_viruses), center_x - 50,
           center_y, font, (Color){255, 150, 150, 255});

  // Attack indicators
  if (player_attacks > 0) {
    center_y += 50;
    DrawText(TextFormat("Attack: %d", player_attacks), center_x - 40,
             center_y, 24, (Color){255, 80, 80, 255});
  }
  if (bot_attacks > 0) {
    center_y += 30;
    DrawText(TextFormat("Attack: %d", bot_attacks), center_x - 40, center_y,
             24, (Color){255, 80, 80, 255});
  }

  // W/L record
  int record_y = layout.left_y + layout.board_h - 80;
  DrawText(TextFormat("W: %d", wins), center_x - 40, record_y, font,
           (Color){100, 255, 100, 255});
  DrawText(TextFormat("L: %d", losses), center_x + 20, record_y, font,
           (Color){255, 100, 100, 255});
  int total = wins + losses;
  if (total > 0) {
    int pct = (wins * 100) / total;
    DrawText(TextFormat("%d%%", pct), center_x + 80, record_y, font,
             (Color){255, 255, 100, 255});
  }
}

// ====================== ROUND END SCREEN ======================

void draw_round_end_screen(bool player_won, int wins, int losses,
                           int round_num, int sw, int sh) {
  // Dark overlay
  DrawRectangle(0, 0, sw, sh, (Color){0, 0, 0, 160});

  int center_x = sw / 2;

  if (player_won) {
    const char *msg = "WINNER!";
    int font = 72;
    int w = MeasureText(msg, font);
    DrawText(msg, center_x - w / 2, sh / 2 - 120, font,
             (Color){100, 255, 100, 255});
  } else {
    const char *msg = "YOU LOSE";
    int font = 72;
    int w = MeasureText(msg, font);
    DrawText(msg, center_x - w / 2, sh / 2 - 120, font,
             (Color){255, 100, 100, 255});
  }

  // Round info
  int font2 = 30;
  DrawText(TextFormat("Round %d", round_num), center_x - 50, sh / 2 - 20,
           font2, RAYWHITE);

  // W/L record
  DrawText(TextFormat("Wins: %d", wins), center_x - 100, sh / 2 + 30, font2,
           (Color){100, 255, 100, 255});
  DrawText(TextFormat("Losses: %d", losses), center_x + 20, sh / 2 + 30,
           font2, (Color){255, 100, 100, 255});

  // Win rate bar
  int total = wins + losses;
  if (total > 0) {
    int bar_w = 300;
    int bar_h = 20;
    int bar_x = center_x - bar_w / 2;
    int bar_y = sh / 2 + 80;
    int win_w = (wins * bar_w) / total;
    DrawRectangle(bar_x, bar_y, win_w, bar_h, (Color){100, 255, 100, 255});
    DrawRectangle(bar_x + win_w, bar_y, bar_w - win_w, bar_h,
                  (Color){255, 100, 100, 255});

    int pct = (wins * 100) / total;
    DrawText(TextFormat("%d%%", pct), bar_x + bar_w + 10, bar_y - 2, font2,
             (Color){255, 255, 100, 255});
  }

  // Instructions
  const char *instr = "Press any key to continue  |  ESC to quit";
  int instr_font = 20;
  int instr_w = MeasureText(instr, instr_font);
  DrawText(instr, center_x - instr_w / 2, sh / 2 + 140, instr_font,
           (Color){160, 160, 160, 255});
}

// ====================== BOT BATTLE HUD ======================

void draw_bot_battle_hud(const std::string &bot1_name,
                         const std::string &bot2_name, int wins1, int wins2,
                         int trial, int total_trials,
                         const LayoutMetrics &layout) {
  // Labels above boards
  int font = 24;
  DrawText(bot1_name.c_str(), layout.left_x, layout.left_y - 40, font,
           (Color){100, 255, 100, 255});
  DrawText(bot2_name.c_str(), layout.right_x, layout.right_y - 40, font,
           (Color){255, 100, 100, 255});

  // Center HUD
  int center_x = (layout.left_x + layout.board_w + layout.right_x) / 2;
  int center_y = layout.left_y + 20;

  DrawText(TextFormat("Trial %d/%d", trial + 1, total_trials), center_x - 50,
           center_y, font, RAYWHITE);

  center_y += 50;
  DrawText(TextFormat("%s: %d", bot1_name.c_str(), wins1), center_x - 60,
           center_y, font, (Color){100, 255, 100, 255});
  center_y += 35;
  DrawText(TextFormat("%s: %d", bot2_name.c_str(), wins2), center_x - 60,
           center_y, font, (Color){255, 100, 100, 255});
}

// ====================== BOT BATTLE RESULT ======================

void draw_bot_battle_result_screen(const std::string &bot1_name,
                                   const std::string &bot2_name, int wins1,
                                   int wins2, int sw, int sh) {
  // Dark overlay
  DrawRectangle(0, 0, sw, sh, (Color){0, 0, 0, 180});

  int center_x = sw / 2;
  int font_title = 48;
  int font_body = 32;
  int font_small = 24;

  // Title
  const char *title = "TOURNAMENT RESULTS";
  int tw = MeasureText(title, font_title);
  DrawText(title, center_x - tw / 2, sh / 2 - 160, font_title, RAYWHITE);

  // Scores
  DrawText(TextFormat("%s: %d wins", bot1_name.c_str(), wins1),
           center_x - 150, sh / 2 - 60, font_body,
           (Color){100, 255, 100, 255});
  DrawText(TextFormat("%s: %d wins", bot2_name.c_str(), wins2),
           center_x - 150, sh / 2 - 20, font_body,
           (Color){255, 100, 100, 255});

  // Winner
  const char *winner;
  Color win_clr;
  if (wins1 > wins2) {
    winner = bot1_name.c_str();
    win_clr = (Color){100, 255, 100, 255};
  } else if (wins2 > wins1) {
    winner = bot2_name.c_str();
    win_clr = (Color){255, 100, 100, 255};
  } else {
    winner = "TIE";
    win_clr = (Color){255, 255, 100, 255};
  }

  const char *win_text =
      (wins1 == wins2)
          ? "It's a tie!"
          : TextFormat("%s wins the series!", winner);
  int wt_w = MeasureText(win_text, font_title);
  DrawText(win_text, center_x - wt_w / 2, sh / 2 + 40, font_title, win_clr);

  // Instructions
  const char *instr = "Press any key to return to title  |  ESC to quit";
  int instr_w = MeasureText(instr, font_small);
  DrawText(instr, center_x - instr_w / 2, sh / 2 + 120, font_small,
           (Color){160, 160, 160, 255});
}
