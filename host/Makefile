CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -I../src
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -I../src
SRC = main.cpp hal_host.cpp ../src/isa.cpp ../src/vm.cpp ../src/runtime.cpp ../src/registry.cpp
BIN = sakura_vm_host
AI_BIN = test_ai_features

$(BIN): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(BIN) $(SRC)

run: $(BIN)
	./$(BIN)

# Edge-AI feature-parity test: the C MFCC must match textochip-ml's golden vectors.
$(AI_BIN): test_ai_features.c ../src/ai/features.c ../src/ai/features.h
	$(CC) $(CFLAGS) -o $(AI_BIN) test_ai_features.c ../src/ai/features.c -lm

test-ai: $(AI_BIN)
	./$(AI_BIN)

clean:
	rm -f $(BIN) $(AI_BIN)

.PHONY: run test-ai clean
