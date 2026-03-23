CXX = g++
CXXFLAGS = -std=c++17 -O2
TARGET = drmario
SRC = drmario.cpp

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)

.PHONY: clean
