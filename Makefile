# limbo-c++ — proprietary software, all rights reserved.
# Copyright (c) 2026 Paranthaman
# See LICENSE. No permission is granted to copy, modify, or redistribute
# this file. Contact Paranthaman-K6@users.noreply.github.com for permission.

CXX ?= g++
CXXFLAGS ?= -std=c++20 -O2 -Wall -Wextra -Wpedantic -Isrc -I. -Ithird_party
LDFLAGS ?= -lcrypto -pthread

# Workstream C done + verified by lead: schematic green, linked with third_party/miniz.c.
SRC := $(shell find src -name '*.cpp') third_party/miniz.c
OBJ := $(SRC:%.cpp=build/obj/%.o)
BIN := build/limbo

TEST_BINS := build/test_varint build/test_buffer build/test_velocity build/test_nbt build/test_registry build/test_void_chunk build/test_play build/test_limits build/test_schematic build/test_versions build/test_miniz

.PHONY: all clean test

all: $(BIN)

$(BIN): $(SRC)
	@mkdir -p $(dir $@) build/obj
	$(CXX) $(CXXFLAGS) $(SRC) -o $@ $(LDFLAGS)

build/obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/test_varint: tests/test_varint.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/test_buffer: tests/test_buffer.cpp src/protocol/buffer.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/test_velocity: tests/test_velocity.cpp src/protocol/buffer.cpp src/security/velocity.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/test_nbt: tests/test_nbt.cpp src/protocol/nbt.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/test_registry: tests/test_registry.cpp src/protocol/registry.cpp src/protocol/nbt.cpp src/protocol/buffer.cpp src/protocol/codec_data.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/test_void_chunk: tests/test_void_chunk.cpp src/world/void_chunk.cpp src/protocol/buffer.cpp src/protocol/codec_data.cpp src/world/schematic.cpp third_party/miniz.c
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/test_play: tests/test_play.cpp src/protocol/play.cpp src/protocol/buffer.cpp src/protocol/codec_data.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/test_limits: tests/test_limits.cpp src/security/limits.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/test_schematic: tests/test_schematic.cpp src/world/schematic.cpp third_party/miniz.c
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/test_miniz: tests/test_miniz.cpp third_party/miniz.c
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/test_versions: tests/test_versions.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

test: $(BIN) $(TEST_BINS)
	./build/test_varint
	./build/test_buffer
	./build/test_velocity
	./build/test_nbt
	./build/test_registry
	./build/test_void_chunk
	./build/test_play
	./build/test_limits
	./build/test_schematic
	./build/test_versions
	./build/test_miniz
	python3 tests/test_velocity_hmac.py

clean:
	rm -rf build
