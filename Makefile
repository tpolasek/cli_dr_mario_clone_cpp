CXX = g++
CXXFLAGS = -std=c++17 -O2
TARGET = drmario
SRC = drmario.cpp board.cpp renderer.cpp bot_ai.cpp bot_registry.cpp bot_random.cpp terminal_io.cpp
HDR = constants.h board.h renderer.h bot_ai.h bot_registry.h bot_random.h terminal_io.h

$(TARGET): $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: clean
