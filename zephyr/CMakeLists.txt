cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(textochip_runtime)

# The portable core lives in ../src and is shared with the host build verbatim.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_sources(app PRIVATE
  src/main.cpp
  src/hal_zephyr.cpp        # the board-specific HAL
  src/arducam_mega.cpp      # the SEE() camera (SPI); compiles to a no-op with no camera node
  ../src/isa.cpp
  ../src/vm.cpp
  ../src/runtime.cpp
  ../src/registry.cpp
)

# ── Edge-AI tier (CONFIG_TEXTOCHIP_AI, default y; boards without a mic wired
# turn it off in boards/<target>.conf — e.g. the Nordic DK phase-1 bring-up).
# When off, AISTART/INFER still execute in the VM; VOICE()/SEE() read "none".
#
# The CLASSIFIER behind VOICE() has two interchangeable backends, both
# implementing the same ai_service:: interface (so runtime.cpp is unchanged;
# exactly ONE is compiled):
#   • default          → ai_service.cpp  : MFCC (features.c) + int8 TFLite Micro
#   • NRF_EDGEAI (B)    → ai_nrf_edgeai.cpp: Nordic's end-to-end streaming KWS
#                         model on the Axon NPU (nrf_edgeai lib, sdk-edge-ai).
if(CONFIG_TEXTOCHIP_AI)
  target_compile_definitions(app PRIVATE TEXTOCHIP_AI)
  # ── Vision tier (SEE()/SEEX()/SEESIZE()) ────────────────────────────────────
  # The camera service is shared; which BACKEND turns its frame into a class is
  # decided by the board's devicetree. A board that wires the Arducam Mega gets
  # the COLOUR path (tc_detect_color_blob over an RGB frame) — the one that
  # actually works today and needs no trained model. A board with no camera node
  # keeps the object-model placeholder, so SEE() reads "nothing" and links.
  target_sources(app PRIVATE ../src/ai/vision_service.cpp)
  dt_nodelabel(tc_cam_node NODELABEL "arducam")
  if(DEFINED tc_cam_node)
    target_compile_definitions(app PRIVATE TEXTOCHIP_VISION_COLOR)
    target_sources(app PRIVATE ../src/ai/color_detect.c)
  else()
    target_sources(app PRIVATE ../src/ai/ai_vision_stub.cpp)
  endif()

if(CONFIG_TEXTOCHIP_NRF_EDGEAI)
  # ── Nordic nRF Edge AI backend (Axon NPU) ───────────────────────────────────
  # ai_nrf_edgeai.cpp implements ai_service:: against Nordic's streaming KWS
  # model. The nrf_edgeai library + Axon driver link automatically from the
  # sdk-edge-ai module (CONFIG_NRF_EDGEAI/NRF_AXON, added via EXTRA_ZEPHYR_MODULES).
  # We compile the GENERATED model + reference its headers FROM the add-on tree
  # (proprietary, LicenseRef-Nordic-5-Clause — referenced, never copied into this
  # open repo). Override TC_EDGEAI_MODEL_DIR with -D if the add-on is elsewhere.
  target_sources(app PRIVATE ../src/ai/ai_nrf_edgeai.cpp)
  target_compile_definitions(app PRIVATE TEXTOCHIP_NRF_EDGEAI)
  set(TC_EDGEAI_MODEL_DIR "$ENV{HOME}/projects/sdk-edge-ai/applications/ww_kws/src/kws/nrf_edgeai_generated"
      CACHE PATH "Nordic KWS generated-model directory (from sdk-edge-ai)")
  if(NOT EXISTS "${TC_EDGEAI_MODEL_DIR}/nrf_edgeai_user_model.c")
    message(FATAL_ERROR "TEXTOCHIP_NRF_EDGEAI: model not found at ${TC_EDGEAI_MODEL_DIR} "
                        "— clone github.com/nrfconnect/sdk-edge-ai and/or set -DTC_EDGEAI_MODEL_DIR=")
  endif()
  target_sources(app PRIVATE ${TC_EDGEAI_MODEL_DIR}/nrf_edgeai_user_model.c)
  target_include_directories(app PRIVATE ${TC_EDGEAI_MODEL_DIR})
else()
  # ── TFLite Micro backend (host / ESP32 / nRF54L-A) ──────────────────────────
  target_sources(app PRIVATE
    ../src/ai/ai_service.cpp
    ../src/ai/features.c
    ../src/ai/ai_host.cpp    # the TFLM backend of ai_infer (host + ESP32 — same code)
  )
  target_compile_definitions(app PRIVATE TF_LITE_STATIC_MEMORY)

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
  # Optimized CMSIS-NN kernels on Cortex-M (int8 conv/depthwise/fc/pool run on
  # the DSP extension — bench: the reference-kernel invoke took ~846 ms/window on
  # the DK's M33). The glue keys on this Kconfig symbol; we include the glue
  # manually (the tflite-micro module isn't in the NCS manifest), so set the
  # trigger at CMake level and enable the CMSIS-NN module ops in prj.conf.
  if(CONFIG_CPU_CORTEX_M)
    set(CONFIG_TENSORFLOW_LITE_MICRO_CMSIS_NN_KERNELS TRUE)
  endif()
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
endif()  # NRF_EDGEAI vs TFLM backend
endif()  # CONFIG_TEXTOCHIP_AI
