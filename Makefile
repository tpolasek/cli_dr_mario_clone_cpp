CXX = g++
CXXFLAGS = -std=c++17 -O3 -I.
TARGET = drmario
SRC = drmario.cpp board.cpp renderer.cpp bot/*.cpp terminal_io.cpp
HDR = constants.h board.h renderer.h bot/*.h terminal_io.h

$(TARGET): $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: clean
