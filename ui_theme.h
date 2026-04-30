#pragma once

#include "gfx.h"
#include "constants.h"

// ====================== RETRO PIXEL THEME ======================
// Unified color palette for the entire UI.
// Retro high-contrast: pure black base, bright saturated NES-style colors.

namespace Theme {

// --- Backgrounds (pure black) ---
constexpr UIColor BG_DEEP{0, 0, 0};
constexpr UIColor BG_BOARD{0, 0, 0};
constexpr UIColor BG_PANEL{10, 10, 10};
constexpr UIColor BG_CELL_EMPTY{10, 10, 10};
constexpr UIColor BG_OVERLAY{0, 0, 0, 180};
constexpr UIColor BG_HIGHLIGHT{255, 255, 255, 50};

// --- Grid ---
constexpr UIColor GRID_LINE{30, 30, 30};

// --- Borders (bright retro gray / white) ---
constexpr UIColor BORDER_BOARD{190, 190, 190};
constexpr UIColor BORDER_PANEL{140, 140, 140};
constexpr UIColor BORDER_NEXT{120, 120, 120};
constexpr UIColor BORDER_GRID{160, 160, 160};

// --- Text (high contrast on black) ---
constexpr UIColor TEXT_TITLE{255, 255, 255};
constexpr UIColor TEXT_PRIMARY{255, 255, 255};
constexpr UIColor TEXT_SECONDARY{180, 180, 180};
constexpr UIColor TEXT_DIM{120, 120, 120};

// --- Status (bright NES palette) ---
constexpr UIColor WIN_GREEN{0, 230, 0};
constexpr UIColor LOSE_RED{255, 50, 50};
constexpr UIColor GOLD{255, 230, 0};
constexpr UIColor ATTACK_RED{255, 80, 60};

// --- Eyes ---
constexpr UIColor EYE_WHITE{255, 255, 255};
constexpr UIColor EYE_PUPIL{0, 0, 0};

// --- Pill Colors (bright NES / highlight / shadow for bevel) ---
constexpr UIColor PILL_RED{230, 50, 50};
constexpr UIColor PILL_RED_LT{255, 130, 130};
constexpr UIColor PILL_RED_DK{160, 30, 30};

constexpr UIColor PILL_YELLOW{255, 230, 40};
constexpr UIColor PILL_YELLOW_LT{255, 250, 150};
constexpr UIColor PILL_YELLOW_DK{180, 160, 20};

constexpr UIColor PILL_BLUE{60, 100, 240};
constexpr UIColor PILL_BLUE_LT{140, 170, 255};
constexpr UIColor PILL_BLUE_DK{30, 60, 160};

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
    case RED:    return {240, 60, 60};
    case YELLOW: return {255, 240, 60};
    case BLUE:   return {70, 130, 255};
    default:     return {128, 128, 128};
    }
}

inline constexpr UIColor virus_legs(int c) {
    switch (c) {
    case RED:    return {180, 40, 40};
    case YELLOW: return {200, 180, 30};
    case BLUE:   return {40, 80, 180};
    default:     return {80, 80, 80};
    }
}

} // namespace Theme
