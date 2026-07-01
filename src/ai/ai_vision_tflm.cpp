// TFLite Micro VISION backend — runs the person-detection model bundled in the TFLM
// submodule (a Phase-0 vision stand-in, like micro_speech was for voice): 96x96
// grayscale, class 0 = not-a-person, 1 = person — which already matches the product's
// SEE() convention (0 = nothing, 1 = "person"). Same shape as ai_host.cpp (the voice
// backend), one model apart. A trained vision-v1 (person/ball/hand) drops in here later.
#include <cstdint>

#include "ai_vision.h"
#include "tensorflow/lite/micro/examples/person_detection/model_settings.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/models/person_detect_model_data.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {

// person_detect needs ~100 KB of working memory. Host-only (the board links the stub
// until a camera + model are brought up), so a generous static arena is fine.
constexpr int kArenaSize = 136 * 1024;
alignas(16) uint8_t g_arena[kArenaSize];
tflite::MicroInterpreter* g_interp = nullptr;

// The 5 ops person_detect uses (see the example's person_detection_test.cc).
using OpResolver = tflite::MicroMutableOpResolver<5>;

bool ensure_init() {
  static bool done = false;
  static bool ok = false;
  if (done) return ok;
  done = true;

  const tflite::Model* model = tflite::GetModel(g_person_detect_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) return false;

  static OpResolver resolver;
  resolver.AddAveragePool2D();
  resolver.AddConv2D();
  resolver.AddDepthwiseConv2D();
  resolver.AddReshape();
  resolver.AddSoftmax();

  static tflite::MicroInterpreter interp(model, resolver, g_arena, kArenaSize);
  if (interp.AllocateTensors() != kTfLiteOk) return false;
  g_interp = &interp;
  ok = true;
  return ok;
}

}  // namespace

extern "C" int ai_infer_vision(const unsigned char* image, int n_pixels) {
  if (!ensure_init() || g_interp == nullptr) return -1;

  TfLiteTensor* input = g_interp->input(0);
  int n = (int)input->bytes < n_pixels ? (int)input->bytes : n_pixels;
  // Camera gives grayscale bytes 0..255; the model input is int8 (-128..127).
  for (int i = 0; i < n; i++) {
    input->data.int8[i] = (int8_t)((int)image[i] - 128);
  }

  if (g_interp->Invoke() != kTfLiteOk) return -1;

  TfLiteTensor* output = g_interp->output(0);
  int best = 0;
  int8_t best_score = output->data.int8[0];
  for (int i = 1; i < kCategoryCount; i++) {
    if (output->data.int8[i] > best_score) {
      best_score = output->data.int8[i];
      best = i;
    }
  }
  return best;  // 0 = not-a-person (nothing), 1 = person
}

extern "C" int ai_vision_num_classes(void) { return kCategoryCount; }
