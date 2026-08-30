CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -Isrc

DSP_SOURCES := src/CharacterProcessor.cpp
TEST_SOURCES := tests/test_character.cpp
TEST_BINARY := build/interface_character_tests

.PHONY: test clean

test: $(TEST_BINARY)
	./$(TEST_BINARY)

$(TEST_BINARY): $(DSP_SOURCES) $(TEST_SOURCES) src/CharacterProcessor.hpp src/Profile.hpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(DSP_SOURCES) $(TEST_SOURCES) -o $@

clean:
	rm -f $(TEST_BINARY)
