cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(textochip_runtime)

# The portable core lives in ../src and is shared with the host build verbatim.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_sources(app PRIVATE
  src/main.cpp
  src/hal_zephyr.cpp        # the ONLY board-specific file
  ../src/isa.cpp
  ../src/vm.cpp
  ../src/runtime.cpp
  ../src/registry.cpp
  # Edge-AI voice tier: the background inference service (mic -> features -> class).
  ../src/ai/ai_service.cpp
  ../src/ai/features.c
  ../src/ai/ai_host.cpp      # the TFLM backend of ai_infer (host + ESP32 — same code)
  # Edge-AI vision tier: the camera service + a placeholder vision backend. The camera
  # (DVP) + a trained vision model are the bench step; SEE() reads "nothing" until then.
  ../src/ai/vision_service.cpp
  ../src/ai/ai_vision_stub.cpp
)
# Turn on the runtime's background inference service (see runtime::tick), and the
# TFLite Micro static-memory mode ai_host.cpp + the TFLM lib compile against.
target_compile_definitions(app PRIVATE TEXTOCHIP_AI TF_LITE_STATIC_MEMORY)

# ── On-device inference: TFLite Micro (reference kernels) ─────────────────────
# Compile TFLM from our vendored submodule (third_party/tflite-micro) with Zephyr's
# official tflite-micro build glue, pointed at the submodule. ai_host.cpp (above) is
# the portable TFLM backend — there is NO separate ai_esp32.cpp because ESP-NN is a
# build-time KERNEL swap, not different code: the reference kernels build for the
# ESP32-S3 (xtensa) today, and ESP-NN drops in later behind the same ai_infer. On a
# Cortex-M target the same glue selects CMSIS-NN.
set(CONFIG_TENSORFLOW_LITE_MICRO TRUE)
set(ZEPHYR_CURRENT_MODULE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tflite-micro)
# Our submodule keeps the vendored third-party deps under tools/make/downloads/
# (populated by `make -C host tflm-lib`), not the glue's third_party_static/ — add
# them so kissfft/gemmlowp/flatbuffers/ruy resolve (kiss_fft_float.cc #includes
# "kiss_fft.c" directly).
set(_tflm_dl ${ZEPHYR_CURRENT_MODULE_DIR}/tensorflow/lite/micro/tools/make/downloads)
zephyr_include_directories(
  ${_tflm_dl}/kissfft
  ${_tflm_dl}/gemmlowp
  ${_tflm_dl}/flatbuffers/include
  ${_tflm_dl}/ruy
)
include(${ZEPHYR_BASE}/modules/tflite-micro/CMakeLists.txt)
# The glue compiles TFLM into its own library; link it into the app image (app's
# ai_host.cpp references the interpreter / kernels / MicroPrintf).
target_link_libraries(app PRIVATE modules__tflite-micro)
