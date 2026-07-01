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
  ../src/ai/ai_stub.cpp      # placeholder ai_infer; the TFLM + ESP-NN backend is
                             # the one remaining board swap (docs/edge-ai.md).
)
# Turn on the runtime's background inference service (see runtime::tick).
target_compile_definitions(app PRIVATE TEXTOCHIP_AI)
