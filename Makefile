CXX = g++
CXXFLAGS = -std=c++17 -O2
TARGET = drmario
SRC = drmario.cpp bot_ai.cpp
HDR = types.h bot_ai.h

$(TARGET): $(SRC) $(HDR)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: clean
