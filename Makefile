CXX = clang++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations
CADICAL_DIR ?= tools/cadical
DRAT_DIR ?= tools/drat-trim-src
BUILD_DIR ?= build
CADICAL_LIB = $(CADICAL_DIR)/build/libcadical.a

.PHONY: all clean
all: $(BUILD_DIR)/queen_prover $(BUILD_DIR)/witness_sa $(BUILD_DIR)/drat-trim

$(BUILD_DIR):
	mkdir -p "$@"

$(BUILD_DIR)/queen_prover: src/queen_prover.cpp $(CADICAL_LIB) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I"$(CADICAL_DIR)/src" "$<" "$(CADICAL_LIB)" -pthread -o "$@"

$(BUILD_DIR)/witness_sa: src/witness_sa.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) "$<" -pthread -o "$@"

$(BUILD_DIR)/drat-trim: $(DRAT_DIR)/drat-trim.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -O2 "$<" -o "$@"

clean:
	$(RM) "$(BUILD_DIR)/queen_prover" "$(BUILD_DIR)/witness_sa" "$(BUILD_DIR)/drat-trim"
