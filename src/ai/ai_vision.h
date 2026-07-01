// Edge-AI VISION inference — maps a camera frame (grayscale image) to a class index.
// This is what the `INFER vision` opcode / `SEE()` reads on the board. Mirrors ai.h
// (the voice/KWS interface), one sense apart.
//
// Backends (one per build):
//   ai_vision_tflm.cpp : TFLite Micro on the person-detection model (host + ESP32) —
//                        a Phase-0 vision stand-in (like micro_speech was for voice).
//   ai_vision_stub.cpp : no model — returns 0 (nothing). The default until a camera +
//                        a trained vision model land. Exactly one backend per build.
#ifndef TEXTOCHIP_AI_AI_VISION_H
#define TEXTOCHIP_AI_AI_VISION_H

#ifdef __cplusplus
extern "C" {
#endif

// Run the vision model on `image` (n_pixels bytes, grayscale 0..255 at the model's
// input size) and return the argmax class index, or -1 on error. By convention class
// 0 = "nothing" (no known object), 1..N = the model's labels — matching VISION_LABELS
// in the product so `SEE()="person"` lines up (person-detection's person index is 1).
// Non-blocking-class: a small vision model runs in a few ms; the service calls this
// between ticks.
int ai_infer_vision(const unsigned char* image, int n_pixels);

// Number of output classes the loaded vision model has (incl. class 0).
int ai_vision_num_classes(void);

#ifdef __cplusplus
}
#endif

#endif  // TEXTOCHIP_AI_AI_VISION_H
