// Fallback edge-AI backend — NO model linked. `ai_infer` always returns 0
// ("none"), so a build that doesn't link a real inference backend still compiles
// and runs the whole capture -> features -> service path; VOICE() simply always
// reads "none". This keeps the default host demo and the current ESP32-S3 board
// build green while the on-device TFLM backend is being brought up.
//
// Exactly ONE ai_* backend is linked per build; each provides these same symbols:
//   ai_stub.cpp  -> this no-op (default host demo + board build, for now)
//   ai_host.cpp  -> TFLite Micro on the PC (the `ai-infer` / voice tests)
//   ai_esp32.cpp -> TFLM + ESP-NN on the ESP32-S3   (planned: the one board swap
//                   that turns the wired-up capture path into real recognition)
#include "ai.h"

extern "C" int ai_infer(const float* /*features*/, int /*n_features*/) { return 0; }

extern "C" int ai_infer_conf(const float* /*features*/, int /*n_features*/,
                             float /*min_conf*/) {
  return 0;
}

extern "C" int ai_num_classes(void) { return 1; }  // just "none"
