CXX = g++
CXXFLAGS = -std=c++17 -O2 -I.
TARGET = drmario
SRC = drmario.cpp board.cpp renderer.cpp bot/bot_ai.cpp bot/bot_registry.cpp bot/bot_random.cpp terminal_io.cpp
HDR = constants.h board.h renderer.h bot/bot_ai.h bot/bot_registry.h bot/bot_random.h terminal_io.h

$(TARGET): $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: clean
