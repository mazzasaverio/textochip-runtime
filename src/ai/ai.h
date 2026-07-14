// Edge-AI inference — maps a feature vector (e.g. an MFCC from features.c) to a
// class index. This is what the `INFER` opcode / `VOICE()` reads on the board.
//
// Backends (one per target, chosen at build time):
//   ai_host.cpp / ai_esp32.cpp : TFLite Micro (+ ESP-NN on ESP32, CMSIS-NN on the
//                                nRF54L Cortex-M33) — fully open (Apache-2.0).
//   ai_nrf54l_npu.cpp          : the Axon NPU (optional, proprietary compiler).
// The trained model + its labels are baked in via a generated model.h / labels
// (exported by textochip-ml). The feature extractor (features.c) and the model's
// feature_params.json are the cross-repo contract.
#ifndef TEXTOCHIP_AI_AI_H
#define TEXTOCHIP_AI_AI_H

#ifdef __cplusplus
extern "C" {
#endif

// Run the model on `features` (n floats, e.g. n_frames*n_mfcc from features.c) and
// return the argmax class index, or -1 on error. By convention class 0 = "none"
// (no/low-confidence keyword), 1..N = the model's words — matching VOICE_LABELS in
// the product so `VOICE()="go"` lines up. Non-blocking-class: keep models small
// (KWS runs in a few ms on the M33/ESP32 CPU).
int ai_infer(const float* features, int n_features);

// Argmax index whose softmax confidence is below `min_conf` (0..1) collapses to 0
// (none) — so a quiet/ambiguous window doesn't trigger a command.
int ai_infer_conf(const float* features, int n_features, float min_conf);

// Raw argmax + its softmax confidence, UN-gated — the bench/tuning view of the
// same inference (what did the model actually think, and how sure was it?).
// Returns the class index (or -1 on error); writes the 0..1 confidence to
// *out_conf when non-null.
int ai_infer_top(const float* features, int n_features, float* out_conf);

// Number of output classes the loaded model has (incl. class 0).
int ai_num_classes(void);

#ifdef __cplusplus
}
#endif

#endif // TEXTOCHIP_AI_AI_H
