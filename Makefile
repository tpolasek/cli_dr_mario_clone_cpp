CXX = g++
CXXFLAGS = -std=c++17 -O3 -I.

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  CXXFLAGS += -I/opt/homebrew/include
  LDFLAGS = -L/opt/homebrew/lib -lraylib -framework CoreVideo -framework IOKit -framework Cocoa -framework GLUT -framework OpenGL
else
  LDFLAGS = -lraylib -lGL -lm -ldl -lpthread -lrt
endif

TARGET = drmario

# Platform-agnostic UI code
UI_SRC = ui_renderer.cpp
# Raylib-specific backend
GFX_SRC = gfx_raylib.cpp
# Shared game code
GAME_SRC = raylib_main.cpp game.cpp board.cpp sound.cpp
# Bot AI
BOT_SRC = $(wildcard bot/*.cpp)
# Headers
HDR = gfx.h sprite_sheet.h ui_theme.h ui_renderer.h gfx_raylib.h \
      constants.h board.h game.h sound.h bot/bot_registry.h $(wildcard bot/bot_*.h)

SRC = $(GAME_SRC) $(UI_SRC) $(GFX_SRC) sprite_sheet.cpp

$(TARGET): $(SRC) $(BOT_SRC) $(HDR)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(BOT_SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: clean
