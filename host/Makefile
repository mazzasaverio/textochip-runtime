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

# Edge-AI end-to-end: audio -> features.c -> ai_infer (TFLite Micro) -> class.
# Needs a built tflite-micro; point TFLM_DIR at it (a third_party/ submodule in
# production). Build the lib once with its own Makefile, then:
#   make ai-infer TFLM_DIR=/path/to/tflite-micro
TFLM_DIR ?= ../third_party/tflite-micro
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
	rm -f $(BIN) $(AI_BIN) test_ai_infer *.o

.PHONY: run test-ai ai-infer clean
