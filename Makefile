CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Iinclude
BUILD := build
COMMON := src/engine.cpp src/replication.cpp

.PHONY: all test bench clean
all: $(BUILD)/minikv $(BUILD)/minikv_tests $(BUILD)/minikv_bench

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/minikv: $(COMMON) src/main.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/minikv_tests: $(COMMON) tests/test_engine.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD)/minikv_bench: $(COMMON) benchmarks/benchmark.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) $^ -o $@

test: $(BUILD)/minikv_tests
	$(BUILD)/minikv_tests

bench: $(BUILD)/minikv_bench
	$(BUILD)/minikv_bench

clean:
	rm -rf $(BUILD)

