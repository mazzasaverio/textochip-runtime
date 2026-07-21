CXX ?= g++
CC ?= gcc
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -I../src
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -I../src
SRC = main.cpp hal_host.cpp ../src/isa.cpp ../src/vm.cpp ../src/runtime.cpp ../src/registry.cpp
BIN = textochip_vm_host
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

# The configurator's VOICE() program drives the motors on the firmware VM (no TFLM —
# the class is injected, the way the inference service will). Proves the same .bas
# the browser sim runs drives the L298N via MOVE, per keyword.
test-ai-move: test_ai_move.cpp ../src/isa.cpp ../src/vm.cpp ../src/registry.cpp hal_host.cpp
	$(CXX) $(CXXFLAGS) -o test_ai_move test_ai_move.cpp ../src/isa.cpp ../src/vm.cpp ../src/registry.cpp hal_host.cpp
	./test_ai_move

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

# Person-detection model + test images (vendored in the TFLM submodule's gen tree) —
# the Phase-0 vision stand-in used by `make test-vision`.
TFLM_GEN = $(TFLM_DIR)/gen/linux_x86_64_default_gcc/genfiles
PERSON_DATA = $(TFLM_GEN)/tensorflow/lite/micro/models/person_detect_model_data.cc \
              $(TFLM_GEN)/tensorflow/lite/micro/examples/person_detection/testdata/person_image_data.cc \
              $(TFLM_GEN)/tensorflow/lite/micro/examples/person_detection/testdata/no_person_image_data.cc

ai-infer: test_ai_infer.c ../src/ai/ai_host.cpp ../src/ai/features.c ../src/ai/models/voice/model.h
	$(CC) $(CFLAGS) -c ../src/ai/features.c -o features_ai.o
	$(CC) $(CFLAGS) -c test_ai_infer.c -o test_ai_infer.o
	$(CXX) $(CXXFLAGS) -DTF_LITE_STATIC_MEMORY $(TFLM_INC) -c ../src/ai/ai_host.cpp -o ai_host.o
	$(CXX) -o test_ai_infer test_ai_infer.o ai_host.o features_ai.o $(TFLM_LIB) -lm
	./test_ai_infer

# Full edge-AI SERVICE end-to-end: TTS speech -> hal mic stub -> ai_service
# (features.c -> ai_infer/TFLM) -> class, then the runtime driving the service so the
# configurator's VOICE() program moves the motors. The real model, no hardware.
# TEXTOCHIP_AI turns on the runtime's service drive (off in the plain host demo).
test-ai-service: test_ai_service.cpp ../src/ai/ai_service.cpp ../src/ai/ai_host.cpp ../src/ai/features.c ../src/runtime.cpp ../src/isa.cpp ../src/vm.cpp ../src/registry.cpp hal_host.cpp ../src/ai/models/voice/model.h
	$(CC) $(CFLAGS) -c ../src/ai/features.c -o features_ai.o
	$(CXX) $(CXXFLAGS) -DTF_LITE_STATIC_MEMORY $(TFLM_INC) -c ../src/ai/ai_host.cpp -o ai_host.o
	$(CXX) $(CXXFLAGS) -DTEXTOCHIP_AI -o test_ai_service test_ai_service.cpp ../src/ai/ai_service.cpp ../src/ai/vision_service.cpp ../src/ai/ai_vision_stub.cpp ../src/runtime.cpp ../src/isa.cpp ../src/vm.cpp ../src/registry.cpp hal_host.cpp ai_host.o features_ai.o $(TFLM_LIB) -lm
	./test_ai_service

# Full VISION end-to-end: a real person-detection image -> hal camera stub ->
# vision_service -> ai_infer_vision (TFLM person-detection) -> class -> SEE() branches.
# A Phase-0 vision stand-in (the bundled person model + test images), no camera.
test-vision: test_vision.cpp ../src/ai/ai_vision_tflm.cpp ../src/ai/vision_service.cpp ../src/isa.cpp ../src/vm.cpp ../src/registry.cpp hal_host.cpp
	$(CXX) $(CXXFLAGS) -DTF_LITE_STATIC_MEMORY $(TFLM_INC) -I$(TFLM_GEN) -c ../src/ai/ai_vision_tflm.cpp -o ai_vision_tflm.o
	$(CXX) $(CXXFLAGS) $(TFLM_INC) -I$(TFLM_GEN) -o test_vision test_vision.cpp ../src/ai/vision_service.cpp ../src/isa.cpp ../src/vm.cpp ../src/registry.cpp hal_host.cpp ai_vision_tflm.o $(PERSON_DATA) $(TFLM_LIB) -lm
	./test_vision

clean:
	rm -f $(BIN) $(AI_BIN) test_ai_infer test_ai_vm test_ai_move test_ai_service test_vision test_color test_color_move test_color_service *.o

# Colour-blob detector for SEE() (the near-term Arducam vision path): synthetic
# RGB frames -> the colour class, pinning the contract with the product's
# VISION_LABELS. Pure C, no TFLM, no camera.
test-color: test_color.c ../src/ai/color_detect.c ../src/ai/color_detect.h
	$(CC) $(CFLAGS) -o test_color test_color.c ../src/ai/color_detect.c -lm
	./test_color

# The colour VISION path end-to-end on the firmware VM (no camera): an RGB frame
# -> color_detect -> SEE() -> the robot stops at yellow / roams otherwise. The
# colour twin of test-ai-move.
test-color-move: test_color_move.cpp ../src/ai/color_detect.c ../src/isa.cpp ../src/vm.cpp ../src/registry.cpp hal_host.cpp
	$(CC) $(CFLAGS) -c ../src/ai/color_detect.c -o color_detect.o
	$(CXX) $(CXXFLAGS) -o test_color_move test_color_move.cpp color_detect.o ../src/isa.cpp ../src/vm.cpp ../src/registry.cpp hal_host.cpp -lm
	./test_color_move

# DIST (ultrasonic distance) end-to-end on the VM (no sensor): the host stub
# feeds a distance -> DISTANCE() -> the robot stops within 10 cm, roams otherwise.
test-dist: test_dist.cpp ../src/isa.cpp ../src/vm.cpp ../src/registry.cpp hal_host.cpp
	$(CXX) $(CXXFLAGS) -o test_dist test_dist.cpp ../src/isa.cpp ../src/vm.cpp ../src/registry.cpp hal_host.cpp -lm
	./test_dist

# The colour VISION SERVICE end-to-end (no camera): an RGB frame -> the camera
# stub (host_feed_rgb) -> vision_service built with TEXTOCHIP_VISION_COLOR (chunked
# capture + tc_detect_color) -> the SEE() class. One layer above test-color.
test-color-service: test_color_service.cpp ../src/ai/vision_service.cpp ../src/ai/color_detect.c hal_host.cpp
	$(CC) $(CFLAGS) -c ../src/ai/color_detect.c -o color_detect.o
	$(CXX) $(CXXFLAGS) -DTEXTOCHIP_VISION_COLOR -c ../src/ai/vision_service.cpp -o vision_service_color.o
	$(CXX) $(CXXFLAGS) -o test_color_service test_color_service.cpp vision_service_color.o color_detect.o hal_host.cpp -lm
	./test_color_service

.PHONY: run test-ai test-ai-vm test-ai-move tflm-lib ai-infer test-ai-service test-vision test-color test-color-move test-color-service test-dist clean
