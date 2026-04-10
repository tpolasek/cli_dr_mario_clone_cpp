#include "renderer.h"
#include <iostream>
#include <vector>
#include <string>

// ====================== BOARD RENDERING ======================

void render_board(const PlayerBoard& board, const char* label, int x_offset, bool show_controls, int attack_count, int anim_frame) {
    struct CellView { std::string txt; };
    std::vector<std::vector<CellView>> buf(ROWS, std::vector<CellView>(COLS));

    const char* virus_char = (anim_frame & 32) ? "\u2742" : "\u2747";

    for (int r = 0; r < ROWS; r++)
        for (int c = 0; c < COLS; c++) {
            if (board.grid[r][c].color != EMPTY) {
                if (board.grid[r][c].virus)
                    buf[r][c].txt = std::string(dark_ansi(board.grid[r][c].color)) + virus_char + virus_char;
                else
                    buf[r][c].txt = std::string(clr_ansi(board.grid[r][c].color)) + "\u2588\u2588";
            } else {
                buf[r][c].txt = "\033[90m ·\033[0m";
            }
        }

    auto overlay = [&](int r, int c, int color) {
        if (r >= 0 && r < ROWS && c >= 0 && c < COLS)
            buf[r][c].txt = std::string(clr_ansi(color)) + "\u2588\u2588";
    };
    if (board.phase == Phase::PLAYING) {
        overlay(board.cap.r1(), board.cap.c1(), board.cap.h1);
        overlay(board.cap.r2(), board.cap.c2(), board.cap.h2);
    }

    std::cout << "\033[" << 4 << ";" << (x_offset + 1) << "H";
    std::cout << "\033[97;1m" << label << "\033[0m";

    std::cout << "\033[" << 5 << ";" << x_offset << "H";
    std::cout << "\033[90m  .----------------.\033[0m";

    for (int r = 0; r < ROWS; r++) {
        std::cout << "\033[" << (6 + r) << ";" << x_offset << "H";
        std::cout << "\033[90m  |\033[0m";
        for (int c = 0; c < COLS; c++)
            std::cout << buf[r][c].txt;
        std::cout << "\033[90m|\033[0m";

        // Info panel
        if (r == 0)  std::cout << " \033[90mScore:\033[0m " << board.score;
        if (r == 2)  std::cout << " \033[90mVirus:\033[0m "
                               << (board.total_viruses - board.cleared_viruses)
                               << "/" << board.total_viruses;
        if (r == 4)  std::cout << " \033[90mNext:\033[0m "
                               << clr_ansi(board.nxt.h1) << "\u2588\u2588"
                               << clr_ansi(board.nxt.h2) << "\u2588\u2588\033[0m";
        if (r == 7)  std::cout << " \033[90mAttack:\033[0m " << attack_count;

        if (show_controls) {
            if (r == 9)  std::cout << " \033[90mA/D  Move\033[0m";
            if (r == 10) std::cout << " \033[90mW    Rotate\033[0m";
            if (r == 11) std::cout << " \033[90mS    Drop\033[0m";
            if (r == 12) std::cout << " \033[90mQ    Quit\033[0m";
        }
    }

    std::cout << "\033[" << (6 + ROWS) << ";" << x_offset << "H";
    std::cout << "\033[90m  '----------------'\033[0m";
}

// ====================== GAME RENDERING ======================

static const char* status_text(const PlayerBoard& player, const PlayerBoard& bot) {
    if (player.game_over) return "\033[91;1m         YOU LOSE! Bot wins!\033[0m";
    if (bot.game_over)    return "\033[92;1m         YOU WIN! Bot lost!\033[0m";
    if (player.phase == Phase::GRAVITY || bot.phase == Phase::GRAVITY)
        return "\033[96m                      Settling...\033[0m";
    return "                      ";
}

void render_game(const PlayerBoard& player, const PlayerBoard& bot,
                 int player_attacks, int bot_attacks, int& anim_frame) {
    anim_frame++;
    std::cout << "\033[2J\033[H";
    std::cout << "\033[97;1m                    DR. MARIO — VS BOT\033[0m\n\n";

    render_board(player, "PLAYER", 2, true, player_attacks, anim_frame);
    render_board(bot, "  BOT", 38, false, bot_attacks, anim_frame);

    int status_row = 6 + ROWS + 2;
    std::cout << "\033[" << status_row << ";1H" << status_text(player, bot);
    std::cout << "\033[" << (status_row + 2) << ";1H";
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
