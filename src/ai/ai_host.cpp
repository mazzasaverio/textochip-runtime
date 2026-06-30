// Edge-AI inference backend — TFLite Micro (host + ESP32; the open default).
// Loads the int8 model baked in by textochip-ml (model.h: g_model[]), runs the
// MicroInterpreter, and returns the argmax class. Same code path as the ESP32
// build (ESP-NN kernels link in there); the nRF54L can use this on the M33
// (CMSIS-NN) or swap to the Axon NPU backend.
//
// The model is the one trained on OUR MFCC (features.c) — the feature contract
// (golden vectors) guarantees the on-device features match training.
#include <cmath>
#include <cstdint>

#include "ai.h"
#include "models/voice/model.h"  // g_model[] / g_model_len — exported by textochip-ml
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

namespace {

// Arena: KWS CNNs need tens of KB. Sized generously for the host; trim per board.
constexpr int kArenaSize = 48 * 1024;
alignas(16) uint8_t g_arena[kArenaSize];

tflite::MicroInterpreter* g_interp = nullptr;

// The op set covering both the demo model and the production kws_cnn:
// Conv2D / DepthwiseConv2D, (max/avg) pooling, GlobalAveragePooling (-> MEAN),
// FullyConnected, Softmax, Reshape, and the elementwise ops a baked-in input
// Normalization lowers to (SUB/MUL/ADD). int8 in/out.
using OpResolver = tflite::MicroMutableOpResolver<14>;

bool ensure_init() {
  // Run the (one-shot) Add* + AllocateTensors exactly once, success or not — the
  // resolver rejects a duplicate AddBuiltin and the interpreter a re-allocation.
  static bool done = false;
  static bool ok = false;
  if (done) return ok;
  done = true;

  const tflite::Model* model = tflite::GetModel(g_model);
  if (model->version() != TFLITE_SCHEMA_VERSION) return ok;

  static OpResolver resolver;
  resolver.AddConv2D();
  resolver.AddDepthwiseConv2D();
  resolver.AddMaxPool2D();
  resolver.AddAveragePool2D();
  resolver.AddMean();
  resolver.AddFullyConnected();
  resolver.AddSoftmax();
  resolver.AddReshape();
  resolver.AddAdd();
  resolver.AddSub();
  resolver.AddMul();

  static tflite::MicroInterpreter interp(model, resolver, g_arena, kArenaSize);
  if (interp.AllocateTensors() != kTfLiteOk) return ok;
  g_interp = &interp;
  ok = true;
  return ok;
}

}  // namespace

extern "C" int ai_num_classes(void) {
  if (!ensure_init()) return -1;
  const TfLiteTensor* out = g_interp->output(0);
  return out->bytes;  // int8 vector, one byte per class
}

// Quantize float features to the model's int8 input, run, return raw argmax.
static int infer_raw(const float* features, int n_features, float* out_conf) {
  if (!ensure_init()) return -1;
  TfLiteTensor* input = g_interp->input(0);
  const float scale = input->params.scale;
  const int zp = input->params.zero_point;
  const int in_count = input->bytes;  // int8
  for (int i = 0; i < in_count; i++) {
    float v = (i < n_features) ? features[i] : 0.0f;
    long q = lround(v / scale) + zp;
    if (q < -128) q = -128;
    if (q > 127) q = 127;
    input->data.int8[i] = (int8_t)q;
  }
  if (g_interp->Invoke() != kTfLiteOk) return -1;

  const TfLiteTensor* output = g_interp->output(0);
  const int n = output->bytes;
  int best = 0;
  int8_t bv = output->data.int8[0];
  for (int i = 1; i < n; i++) {
    if (output->data.int8[i] > bv) {
      bv = output->data.int8[i];
      best = i;
    }
  }
  if (out_conf) {
    // dequantize the winning logit to a 0..1 softmax probability.
    *out_conf = (bv - output->params.zero_point) * output->params.scale;
  }
  return best;
}

extern "C" int ai_infer(const float* features, int n_features) {
  return infer_raw(features, n_features, nullptr);
}

extern "C" int ai_infer_conf(const float* features, int n_features, float min_conf) {
  float conf = 0.0f;
  int cls = infer_raw(features, n_features, &conf);
  if (cls < 0) return -1;
  return (conf >= min_conf) ? cls : 0;  // low confidence -> none
}
