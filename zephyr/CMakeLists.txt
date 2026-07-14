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
)

# ── Edge-AI tier (CONFIG_TEXTOCHIP_AI, default y; boards without a mic wired
# turn it off in boards/<target>.conf — e.g. the Nordic DK phase-1 bring-up).
# When off, AISTART/INFER still execute in the VM; VOICE()/SEE() read "none".
if(CONFIG_TEXTOCHIP_AI)
  target_sources(app PRIVATE
    # The background inference service (mic -> features -> class).
    ../src/ai/ai_service.cpp
    ../src/ai/features.c
    ../src/ai/ai_host.cpp    # the TFLM backend of ai_infer (host + ESP32 — same code)
    # Vision tier: the camera service + a placeholder vision backend. The camera
    # (DVP) + a trained vision model are the bench step; SEE() reads "nothing".
    ../src/ai/vision_service.cpp
    ../src/ai/ai_vision_stub.cpp
  )
  # Turn on the runtime's background inference service (see runtime::tick), and
  # the TFLite Micro static-memory mode ai_host.cpp + the TFLM lib compile against.
  target_compile_definitions(app PRIVATE TEXTOCHIP_AI TF_LITE_STATIC_MEMORY)

  # ── On-device inference: TFLite Micro (reference kernels) ───────────────────
  # Compile TFLM from our vendored submodule (third_party/tflite-micro) with
  # Zephyr's official tflite-micro build glue, pointed at the submodule.
  # ai_host.cpp (above) is the portable TFLM backend — there is NO separate
  # ai_esp32.cpp because ESP-NN is a build-time KERNEL swap, not different code:
  # the reference kernels build for the ESP32-S3 (xtensa) today, and ESP-NN
  # drops in later behind the same ai_infer. On a Cortex-M target the same glue
  # selects CMSIS-NN.
  set(CONFIG_TENSORFLOW_LITE_MICRO TRUE)
  set(ZEPHYR_CURRENT_MODULE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/tflite-micro)
  # Our submodule keeps the vendored third-party deps under tools/make/downloads/
  # (populated by `make -C host tflm-lib`), not the glue's third_party_static/ —
  # add them so kissfft/gemmlowp/flatbuffers/ruy resolve (kiss_fft_float.cc
  # #includes "kiss_fft.c" directly).
  set(_tflm_dl ${ZEPHYR_CURRENT_MODULE_DIR}/tensorflow/lite/micro/tools/make/downloads)
  zephyr_include_directories(
    ${_tflm_dl}/kissfft
    ${_tflm_dl}/gemmlowp
    ${_tflm_dl}/flatbuffers/include
    ${_tflm_dl}/ruy
  )
  include(${ZEPHYR_BASE}/modules/tflite-micro/CMakeLists.txt)
  # The glue compiles TFLM into its own library; link it into the app image
  # (app's ai_host.cpp references the interpreter / kernels / MicroPrintf).
  target_link_libraries(app PRIVATE modules__tflite-micro)
  # CRITICAL: the lib and every TU that includes TFLM headers must agree on
  # TF_LITE_STATIC_MEMORY — it changes the TfLiteTensor STRUCT LAYOUT. The
  # Zephyr glue builds the lib WITHOUT it, while ai_host.cpp defines it (and
  # TFLM's own Makefile, which the host build uses, always defines it). The
  # mismatch made interp.input(0) field reads land on wrong offsets -> a
  # garbage tensor data pointer -> a bus fault on the FIRST on-device
  # inference (bench-proven on the nRF54LM20 DK, 2026-07-14).
  target_compile_definitions(modules__tflite-micro PRIVATE TF_LITE_STATIC_MEMORY)
  # Static-memory mode requires -fno-exceptions (as TFLM's own Makefile sets):
  # TF_LITE_REMOVE_VIRTUAL_DELETE makes each planner's operator delete private,
  # and with exceptions ON the placement-new cleanup lookup in micro_allocator
  # trips over it ("operator delete is private"). The glue only sets -fno-rtti.
  target_compile_options(modules__tflite-micro PRIVATE -fno-exceptions)
endif()
