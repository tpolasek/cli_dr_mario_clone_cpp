CXX = g++
CXXFLAGS = -std=c++17 -O3 -I. -I/opt/homebrew/include
LDFLAGS = -L/opt/homebrew/lib -lraylib -framework CoreVideo -framework IOKit -framework Cocoa -framework GLUT -framework OpenGL

TARGET = drmario
SRC = raylib_main.cpp raylib_renderer.cpp game.cpp board.cpp sound.cpp
BOT_SRC = $(wildcard bot/*.cpp)
HDR = constants.h board.h game.h raylib_renderer.h sound.h bot/bot_registry.h $(wildcard bot/bot_*.h)

$(TARGET): $(SRC) $(BOT_SRC) $(HDR)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(BOT_SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: clean
