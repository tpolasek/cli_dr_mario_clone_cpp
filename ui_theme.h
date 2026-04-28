#pragma once

#include "gfx.h"
#include "constants.h"

// ====================== RETRO PIXEL THEME ======================
// Unified color palette for the entire UI.
// NES-inspired: dark blue-purple base, bright saturated game pieces.

namespace Theme {

// --- Backgrounds ---
constexpr UIColor BG_DEEP{8, 8, 24};
constexpr UIColor BG_BOARD{16, 14, 36};
constexpr UIColor BG_PANEL{22, 18, 46};
constexpr UIColor BG_CELL_EMPTY{20, 16, 42};
constexpr UIColor BG_OVERLAY{0, 0, 0, 180};
constexpr UIColor BG_HIGHLIGHT{255, 255, 255, 50};

// --- Grid ---
constexpr UIColor GRID_LINE{32, 28, 56};

// --- Borders ---
constexpr UIColor BORDER_BOARD{90, 75, 145};
constexpr UIColor BORDER_PANEL{70, 60, 120};
constexpr UIColor BORDER_NEXT{60, 50, 100};
constexpr UIColor BORDER_GRID{100, 85, 160};

// --- Text ---
constexpr UIColor TEXT_TITLE{255, 220, 80};
constexpr UIColor TEXT_PRIMARY{240, 235, 220};
constexpr UIColor TEXT_SECONDARY{160, 152, 180};
constexpr UIColor TEXT_DIM{100, 95, 125};

// --- Status ---
constexpr UIColor WIN_GREEN{80, 220, 80};
constexpr UIColor LOSE_RED{240, 60, 60};
constexpr UIColor GOLD{255, 210, 50};
constexpr UIColor ATTACK_RED{255, 80, 60};

// --- Eyes ---
constexpr UIColor EYE_WHITE{240, 240, 240};
constexpr UIColor EYE_PUPIL{20, 15, 40};

// --- Pill Colors (bright / highlight / shadow for bevel) ---
constexpr UIColor PILL_RED{220, 50, 50};
constexpr UIColor PILL_RED_LT{255, 120, 120};
constexpr UIColor PILL_RED_DK{150, 30, 30};

constexpr UIColor PILL_YELLOW{240, 210, 40};
constexpr UIColor PILL_YELLOW_LT{255, 245, 130};
constexpr UIColor PILL_YELLOW_DK{170, 145, 20};

constexpr UIColor PILL_BLUE{50, 90, 220};
constexpr UIColor PILL_BLUE_LT{120, 155, 255};
constexpr UIColor PILL_BLUE_DK{30, 60, 150};

// --- Pill color lookup ---
inline constexpr UIColor pill_color(int c) {
    switch (c) {
    case RED:    return PILL_RED;
    case YELLOW: return PILL_YELLOW;
    case BLUE:   return PILL_BLUE;
    default:     return {128, 128, 128};
    }
}

inline constexpr UIColor pill_light(int c) {
    switch (c) {
    case RED:    return PILL_RED_LT;
    case YELLOW: return PILL_YELLOW_LT;
    case BLUE:   return PILL_BLUE_LT;
    default:     return {200, 200, 200};
    }
}

inline constexpr UIColor pill_dark(int c) {
    switch (c) {
    case RED:    return PILL_RED_DK;
    case YELLOW: return PILL_YELLOW_DK;
    case BLUE:   return PILL_BLUE_DK;
    default:     return {80, 80, 80};
    }
}

// --- Virus colors (slightly different from pills) ---
inline constexpr UIColor virus_body(int c) {
    switch (c) {
    case RED:    return {230, 65, 65};
    case YELLOW: return {250, 230, 65};
    case BLUE:   return {65, 120, 240};
    default:     return {128, 128, 128};
    }
}

inline constexpr UIColor virus_legs(int c) {
    switch (c) {
    case RED:    return {160, 40, 40};
    case YELLOW: return {180, 155, 35};
    case BLUE:   return {35, 75, 165};
    default:     return {80, 80, 80};
    }
}

} // namespace Theme
