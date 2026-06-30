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

# Firmware VM AISTART/INFER opcode test (no TFLM — the AI class is injected).
test-ai-vm: test_ai_vm.cpp ../src/isa.cpp ../src/vm.cpp ../src/registry.cpp hal_host.cpp
	$(CXX) $(CXXFLAGS) -o test_ai_vm test_ai_vm.cpp ../src/isa.cpp ../src/vm.cpp ../src/registry.cpp hal_host.cpp
	./test_ai_vm

# Edge-AI end-to-end: audio -> features.c -> ai_infer (TFLite Micro) -> class.
# TFLM is the `third_party/tflite-micro` submodule. One-time setup from the repo root:
#   git submodule update --init --depth 1 third_party/tflite-micro
#   make -C host tflm-lib        # builds libtensorflow-microlite.a (~minutes)
# then `make ai-infer` works out of the box (TFLM_DIR can override the location).
TFLM_DIR ?= ../third_party/tflite-micro

# Build the TFLM static lib from the submodule (downloads its deps, ~minutes).
tflm-lib:
	$(MAKE) -C $(TFLM_DIR) -f tensorflow/lite/micro/tools/make/Makefile microlite
TFLM_DL = $(TFLM_DIR)/tensorflow/lite/micro/tools/make/downloads
TFLM_INC = -I$(TFLM_DIR) -I$(TFLM_DL)/flatbuffers/include -I$(TFLM_DL)/gemmlowp \
           -I$(TFLM_DL)/ruy -I$(TFLM_DL)/kissfft
TFLM_LIB = $(shell find $(TFLM_DIR)/gen -name libtensorflow-microlite.a 2>/dev/null | head -1)

ai-infer: test_ai_infer.c ../src/ai/ai_host.cpp ../src/ai/features.c ../src/ai/models/voice/model.h
	$(CC) $(CFLAGS) -c ../src/ai/features.c -o features_ai.o
	$(CC) $(CFLAGS) -c test_ai_infer.c -o test_ai_infer.o
	$(CXX) $(CXXFLAGS) -DTF_LITE_STATIC_MEMORY $(TFLM_INC) -c ../src/ai/ai_host.cpp -o ai_host.o
	$(CXX) -o test_ai_infer test_ai_infer.o ai_host.o features_ai.o $(TFLM_LIB) -lm
	./test_ai_infer

clean:
	rm -f $(BIN) $(AI_BIN) test_ai_infer test_ai_vm *.o

.PHONY: run test-ai test-ai-vm tflm-lib ai-infer clean
