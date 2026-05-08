CXX := g++
CXXFLAGS := -std=c++17 -O3 -Wall -Wextra -Wpedantic -Iinclude

TARGET := build/backtester
SOURCES := src/main.cpp

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SOURCES) include/backtester.hpp include/csv.hpp include/strategy.hpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

run: $(TARGET)
	$(TARGET) configs/default.conf

clean:
	rm -rf build
