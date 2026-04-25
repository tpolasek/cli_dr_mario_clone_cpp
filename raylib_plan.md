# Dr. Mario Raylib Edition — Implementation Plan

## File Changes

| File | Action | Purpose |
|------|--------|---------|
| `raylib_main.cpp` | **NEW** | Entry point, raylib init/window, CLI parsing, scene state machine loop |
| `raylib_renderer.h` | **NEW** | Renderer declarations for raylib drawing |
| `raylib_renderer.cpp` | **NEW** | All drawing: bg, boards, pills, viruses, menus, HUD, round-end overlays |
| `game.h` | **NEW** | Shared declarations (`process_phases`, `new_piece_with_speed`) |
| `game.cpp` | **NEW** | Shared game logic extracted from `drmario.cpp` |
| `Makefile` | **MODIFY** | Single target: `drmario` (raylib) |
| `terminal_io.h/.cpp` | **DELETE** | No longer needed |
| `renderer.h/.cpp` | **DELETE** | Replaced by `raylib_renderer.*` |
| `sound.h/.cpp` | **MODIFY** | Replace fork/afplay with raylib `Music` API |
| `drmario.cpp` | **DELETE** | Replaced by `raylib_main.cpp` + `game.cpp` |
| Everything else | **UNCHANGED** | `board.h/.cpp`, `constants.h`, `bot/*` |

---

## Scene State Machine

```
TITLE ──(select mode)──→ GAME ──(round ends)──→ ROUND_END ──(any key)──→ GAME
  │                        │                                          │
  │                        │ESC                                       │ESC/Q
  │                        ↓                                          ↓
  │                       QUIT ←──────────────────────────────────────┘
  │
  └──(if --bot1/--bot2)──→ BOT_BATTLE ──→ BOT_BATTLE_RESULT ──→ TITLE
```

**ESC always quits the entire game immediately from any scene.**

---

## Scene: TITLE

- **Background**: `bg.png` full-screen, no dimming
- **Animated viruses**: Scatter 20-30 wiggling viruses (same rendering as in-game) at random positions across the screen. They bob/wiggle continuously (medium speed, `sin(time * speed)` on leg angles).
- **Mode options** rendered as centered text:
  ```
  1.  🎀  Jane Mode
        Virus: 3  |  Speed: Slow  |  Bot: Kid

  2.  🎮  Dad Mode
        Virus: 40  |  Speed: Medium  |  Bot: Swift

  3.  🧪  Test Mode
        Virus: 1  |  Speed: Very Slow  |  Bot: Kid
  ```
- **Selection**: `int selected = 0` (0-indexed). The selected row has a **colored bar behind it** (semi-transparent highlight rectangle, e.g. `{255,255,255,80}`) spanning the text width.
- **UP/DOWN** → cycle selection (wrap around 0→2→1→0)
- **Any other key** (Enter, Space, letters, etc.) → confirm selection → `Scene::GAME`
- **ESC** → `Scene::QUIT`

## Scene: GAME

- **Background**: `bg.png` full brightness
- **Two boards** side-by-side (see layout below)
- **Next piece preview** above each board (in a small box)
- **HUD** in the center gap: score, virus count, attack indicator, round number, W/L record
- **Player input**: LEFT/RIGHT/DOWN/UP (arrow keys) = move/soft-drop/rotate
- **Game tick loop** runs inside the raylib frame loop — **60fps rendering + 60 ticks/sec game logic** (every frame = 1 tick + render)
- On round end → `Scene::ROUND_END`
- **ESC** → quit entire game

## Scene: ROUND_END

- **Background**: `bg.png`
- Overlay: Large "WINNER!" or "YOU LOSE" text centered
- W/L record, round number, win-rate bar
- **Any key** → next round (back to `Scene::GAME`)
- **ESC** → quit

## Scene: BOT_BATTLE

- Same layout as GAME but both boards are bot-controlled
- Text overlay showing trial number, running scores
- On all trials complete → `Scene::BOT_BATTLE_RESULT`
- **ESC** → quit

## Scene: BOT_BATTLE_RESULT

- Final scores table, winner announcement
- **Any key** → `Scene::TITLE`
- **ESC** → quit

---

## Board Layout (percentage-based)

```cpp
// Configurable
float BOARD_LEFT_PCT  = 0.10f;
float BOARD_RIGHT_PCT = 0.10f;

// At 1920×1080:
int cell_size = (int)(1080 * 0.80) / ROWS;  // ≈ 54px, board = 432×864

// Left board
int left_x = 1920 * 0.10 = 192
int left_y = (1080 - 864) / 2 = 108

// Right board  
int right_x = 1920 - 192 - 432 = 1296
int right_y = 108

// Center gap = 1296 - (192+432) = 672px for HUD
```

---

## Rendering

### Capsule/Pill
- Two connected halves → single **rounded rectangle** spanning both cells using `DrawRectangleRounded()` or manual: `DrawCircle` at each cell center + `DrawRectangle` bridge
- Single half → small rounded square

### Virus
- Solid filled circle (`DrawCircle`) in bright color
- 4 small "legs" — short lines at diagonal angles from the circle edge
- Legs oscillate: `angle_offset = sin(time * 3.0 + seed) * 0.3` — medium wiggle
- Body color bright, legs slightly darker shade

### Colors
```cpp
RED    → {220, 50, 50}    legs: {160, 30, 30}
YELLOW → {255, 220, 50}   legs: {180, 150, 30}
BLUE   → {50, 100, 220}   legs: {30, 70, 160}
```

### Board Grid
- Subtle grid lines (dark gray, thin) for empty cells
- Colored fills for occupied cells

### Next Piece Preview
- Small box **above each board** showing the next capsule

---

## Timing & FPS

- **Window**: 1920×1080, not fullscreen
- **Render**: 60fps (every raylib frame)
- **Game tick**: 60 ticks/sec (1 tick per frame, same as `GAME_FPS`)
- **Bot input**: throttled by `BOT_INPUT_TICK_RATE` (every 10 ticks)
- **Gravity**: throttled by `GRAVITY_TICK_RATE` (every 20 ticks)
- No separate `RENDER_FPS` throttle — render every frame for smooth animation

---

## Sound (raylib built-in)

```cpp
InitAudioDevice();
Music music = LoadMusicStream("queque.mp3");

// Start: PlayMusicStream(music);
// Each frame: UpdateMusicStream(music);
// Stop: StopMusicStream(music);

// Cleanup
UnloadMusicStream(music);
CloseAudioDevice();
```

---

## Title Screen Viruses

- Generate ~25 random positions on screen at startup
- Each virus has: `{x, y, color, phase_offset}`
- Draw them wiggling continuously behind the menu text
- They're purely decorative, no gameplay interaction

---

## Build (Makefile)

```makefile
# Single target: raylib GUI
drmario: raylib_main.o raylib_renderer.o game.o board.o bot/*.o
	$(CXX) $(CXXFLAGS) -o drmario $^ $(RAYLIB_LDFLAGS)
```

Raylib link flags: `-lraylib -framework CoreVideo -framework IOKit -framework Cocoa -framework GLUT -framework OpenGL`

---

## CLI Args (`raylib_main.cpp`)

```
./drmario                    → GUI title screen (Scene::TITLE)
./drmario --bot1 X --bot2 Y  → GUI bot-vs-bot (Scene::BOT_BATTLE)
./drmario --seed N           → seed for bot battles
```

---

## Input Mapping

| Scene | Key | Action |
|-------|-----|--------|
| TITLE | UP/DOWN | Change selection |
| TITLE | Any other key | Confirm selection |
| TITLE | ESC | Quit |
| GAME | LEFT/RIGHT | Move capsule |
| GAME | DOWN | Soft drop |
| GAME | UP | Rotate |
| GAME | ESC | Quit entire game |
| ROUND_END | Any key | Next round |
| ROUND_END | ESC/Q | Quit |
| BOT_BATTLE_RESULT | Any key | Back to TITLE |
| BOT_BATTLE_RESULT | ESC | Quit |
