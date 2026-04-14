#include "renderer.h"
#include <iostream>
#include <string>
#include <vector>
#include <cstring>

// ====================== BLOCK RENDERING CONFIG ======================
// Each game cell is rendered as BW chars wide × BH terminal rows tall.
// This gives a much larger, more visually appealing board.
static constexpr int BW = 4;                       // chars per cell width
static constexpr int BH = 2;                       // rows per cell height
static constexpr int BOARD_CW = COLS * BW;         // 32 chars content width
static constexpr int BOARD_CH = ROWS * BH;         // 32 rows content height

// ====================== VIRUS BACKGROUND COLORS ======================
// Viruses render with ANSI background-color fills (no gaps) instead of
// foreground characters, making them instantly distinguishable from capsules.

inline const char* virus_bright_bg(int c) {
    switch (c) {
        case RED:    return "\033[101m";   // bright red bg
        case YELLOW: return "\033[103m";   // bright yellow bg
        case BLUE:   return "\033[104m";   // bright blue bg
        default:     return "\033[0m";
    }
}

inline const char* virus_dark_bg(int c) {
    switch (c) {
        case RED:    return "\033[41m";    // dark red bg
        case YELLOW: return "\033[43m";    // dark yellow bg
        case BLUE:   return "\033[44m";    // dark blue bg
        default:     return "\033[0m";
    }
}

// ====================== BOARD RENDERING ======================

void render_board(const PlayerBoard& board, const char* label, int x_offset,
                  bool show_controls, int attack_count, int anim_frame)
{
    // ---- Build cell view buffer ----
    struct CellView { std::string rows[BH]; };
    std::vector<std::vector<CellView>> buf(ROWS, std::vector<CellView>(COLS));

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            const Piece& p = board.grid[r][c];
            if (p.color == EMPTY) {
                // Empty: subtle dot grid (vertical divider between cells)
                buf[r][c].rows[0] = "\033[90m   ·\033[0m";
                buf[r][c].rows[1] = "\033[90m   ·\033[0m";
            } else if (p.virus) {
                // Virus: diagonal shading — TL/BR dark, TR/BL bright, with flash
                const bool flash = (anim_frame & 32);
                const char* d = flash ? virus_dark_bg(p.color) : virus_bright_bg(p.color);
                const char* b = flash ? virus_bright_bg(p.color) : virus_dark_bg(p.color);
                buf[r][c].rows[0] = std::string(d) + "  " + std::string(b) + "  " + "\033[0m";
                buf[r][c].rows[1] = std::string(b) + "  " + std::string(d) + "  " + "\033[0m";
            } else {
                // Capsule piece: uniform bright fill (no shading)
                buf[r][c].rows[0] = std::string(virus_bright_bg(p.color)) + "    " + "\033[0m";
                buf[r][c].rows[1] = std::string(virus_bright_bg(p.color)) + "    " + "\033[0m";
            }
        }
    }

    // Overlay the active capsule (only during PLAYING phase)
    auto overlay = [&](int r, int c, int color) {
        if (r >= 0 && r < ROWS && c >= 0 && c < COLS) {
            buf[r][c].rows[0] = std::string(virus_bright_bg(color)) + "    " + "\033[0m";
            buf[r][c].rows[1] = std::string(virus_bright_bg(color)) + "    " + "\033[0m";
        }
    };
    if (board.phase == Phase::PLAYING) {
        overlay(board.cap.r1(), board.cap.c1(), board.cap.h1);
        overlay(board.cap.r2(), board.cap.c2(), board.cap.h2);
    }

    // ---- Layout ----
    // Row 3 : label (centred above board)
    // Row 4 : score · virus count · attack indicator
    // Row 5 : next-piece preview
    // Row 6 : ╔══...══╗  (top border)
    // Rows 7–38 : grid (16 cells × 2 sub-rows = 32 terminal rows)
    // Row 39 : ╚══...══╝  (bottom border)

    const int label_row = 3;
    const int info_row  = 4;
    const int next_row  = 5;
    const int top_row   = 6;

    // Label (centred over the board)
    int lbl_len = (int)std::strlen(label);
    int lbl_x   = x_offset + 1 + (BOARD_CW - lbl_len) / 2;
    std::cout << "\033[" << label_row << ";" << lbl_x << "H";
    std::cout << "\033[97;1m" << label << "\033[0m";

    // Info line: score · virus · attack
    std::cout << "\033[" << info_row << ";" << x_offset << "H";
    std::cout << " \033[90mScore\033[0m " << board.score;
    int rem = board.total_viruses - board.cleared_viruses;
    std::cout << "  \033[90mVirus\033[0m " << rem << "/" << board.total_viruses;
    if (attack_count > 0)
        std::cout << "  \033[91;1m⚡" << attack_count << "\033[0m";

    // Next-piece preview (rendered with same block style)
    std::cout << "\033[" << next_row << ";" << x_offset << "H";
    std::cout << " \033[90mNext\033[0m ";
    std::cout << clr_ansi(board.nxt.h1) << "████" << "\033[0m";
    std::cout << clr_ansi(board.nxt.h2) << "████" << "\033[0m";

    // Top border
    std::cout << "\033[" << top_row << ";" << x_offset << "H";
    std::cout << "\033[90m╔";
    for (int i = 0; i < BOARD_CW; i++) std::cout << "═";
    std::cout << "╗\033[0m";

    // Grid rows
    for (int r = 0; r < ROWS; r++) {
        for (int s = 0; s < BH; s++) {
            int term_row = top_row + 1 + r * BH + s;
            std::cout << "\033[" << term_row << ";" << x_offset << "H";
            std::cout << "\033[90m║\033[0m";
            for (int c = 0; c < COLS; c++)
                std::cout << buf[r][c].rows[s];
            std::cout << "\033[90m║\033[0m";

            // Side info panel (every other sub-row, on the second sub-row of select cells)
            if (s == 1) {
                int info_x = x_offset + BOARD_CW + 4;
                if (r == 0) {
                    std::cout << "\033[" << term_row << ";" << info_x << "H";
                    std::cout << "\033[90mScore:\033[0m " << board.score;
                }
                if (r == 2) {
                    std::cout << "\033[" << term_row << ";" << info_x << "H";
                    std::cout << "\033[90mVirus:\033[0m " << rem << "/" << board.total_viruses;
                }
                if (r == 4) {
                    std::cout << "\033[" << term_row << ";" << info_x << "H";
                    std::cout << "\033[90mNext:\033[0m "
                              << clr_ansi(board.nxt.h1) << "██\033[0m"
                              << clr_ansi(board.nxt.h2) << "██\033[0m";
                }
                if (r == 7 && attack_count > 0) {
                    std::cout << "\033[" << term_row << ";" << info_x << "H";
                    std::cout << "\033[91;1mAttack: " << attack_count << "\033[0m";
                }
                if (show_controls) {
                    if (r == 9) {
                        std::cout << "\033[" << term_row << ";" << info_x << "H";
                        std::cout << "\033[90mA/D  Move\033[0m";
                    }
                    if (r == 10) {
                        std::cout << "\033[" << term_row << ";" << info_x << "H";
                        std::cout << "\033[90mW    Rotate\033[0m";
                    }
                    if (r == 11) {
                        std::cout << "\033[" << term_row << ";" << info_x << "H";
                        std::cout << "\033[90mS    Drop\033[0m";
                    }
                    if (r == 12) {
                        std::cout << "\033[" << term_row << ";" << info_x << "H";
                        std::cout << "\033[90mQ    Quit\033[0m";
                    }
                }
            }
        }
    }

    // Bottom border
    int bottom_row = top_row + 1 + BOARD_CH;
    std::cout << "\033[" << bottom_row << ";" << x_offset << "H";
    std::cout << "\033[90m╚";
    for (int i = 0; i < BOARD_CW; i++) std::cout << "═";
    std::cout << "╝\033[0m";
}

// ====================== GAME RENDERING ======================

static const char* status_text(const PlayerBoard& player, const PlayerBoard& bot) {
    if (player.game_over) return "\033[91;1m           YOU LOSE!  Bot wins!\033[0m";
    if (bot.game_over)    return "\033[92;1m           YOU WIN!   Bot lost!\033[0m";
    if (player.phase == Phase::GRAVITY || bot.phase == Phase::GRAVITY)
        return "\033[96m               Settling...\033[0m";
    return "                                 ";
}

void render_game(const PlayerBoard& player, const PlayerBoard& bot,
                 int player_attacks, int bot_attacks, int& anim_frame) {
    anim_frame++;
    std::cout << "\033[2J\033[H";
    std::cout << "\033[97;1m                  DR. MARIO — VS BOT\033[0m";

    // Board x positions: player at col 2, bot at col 50
    render_board(player, "PLAYER", 2, true, player_attacks, anim_frame);
    render_board(bot,      "BOT",   50, false, bot_attacks, anim_frame);

    // Status line below the boards
    const int top_row    = 6;
    const int bottom_row = top_row + 1 + BOARD_CH;
    const int status_row = bottom_row + 2;

    std::cout << "\033[" << status_row << ";1H" << status_text(player, bot);

    // Controls footer
    std::cout << "\033[" << (status_row + 1) << ";1H";
    std::cout << "\033[90m     A/D Move  ·  W Rotate  ·  S Drop  ·  Q Quit\033[0m";

    // Park cursor out of the way
    std::cout << "\033[" << (status_row + 3) << ";1H";
    std::cout.flush();
}

// ====================== MENU RENDERING ======================

void render_enter_alt_screen() {
    std::cout << "\033[?1049h\033[?25l\033[2J\033[H";
    std::cout.flush();
}

void render_clear_screen() {
    std::cout << "\033[2J\033[H";
}

void render_title() {
    std::cout << "\n\n    \033[97;1m  DR. MARIO — VS BOT\033[0m  (Terminal Edition)\n\n";
}

void render_virus_menu() {
    std::cout << "    Select virus count:\n"
              << "      \033[93m[1]\033[0m  Low    ( 5)\n"
              << "      \033[93m[2]\033[0m  Medium (10)\n"
              << "      \033[93m[3]\033[0m  High   (20)\n"
              << "      \033[93m[4]\033[0m  Ultra  (30)\n\n    > ";
    std::cout.flush();
}

void render_speed_menu() {
    std::cout << "\n    Select speed:\n"
              << "      \033[93m[1]\033[0m  Low\n"
              << "      \033[93m[2]\033[0m  Medium\n"
              << "      \033[93m[3]\033[0m  High\n\n    > ";
    std::cout.flush();
}

void render_bot_menu(const std::vector<BotEntry>& bots) {
    std::cout << "\n    Select bot opponent:\n";
    for (size_t i = 0; i < bots.size(); i++) {
        std::cout << "      \033[93m[" << (i + 1) << "]\033[0m  "
                  << bots[i].name
                  << " \033[90m— " << bots[i].description << "\033[0m\n";
    }
    std::cout << "\n    > ";
    std::cout.flush();
}
