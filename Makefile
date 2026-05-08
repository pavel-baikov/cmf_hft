CXX := g++
CXXFLAGS := -std=c++17 -O3 -Wall -Wextra -Wpedantic -Iinclude

TARGET      := build/backtester
TEST_TARGET := build/test_engine
SOURCES     := src/main.cpp
TEST_SOURCES:= tests/test_engine.cpp

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(SOURCES) include/backtester.hpp include/csv.hpp include/strategy.hpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

$(TEST_TARGET): $(TEST_SOURCES) include/backtester.hpp include/csv.hpp include/strategy.hpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(TEST_SOURCES) -o $(TEST_TARGET)

test: $(TEST_TARGET)
	./$(TEST_TARGET)

run: $(TARGET)
	$(TARGET) configs/default.conf

clean:
	rm -rf build
