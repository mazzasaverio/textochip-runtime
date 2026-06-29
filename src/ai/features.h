// On-device MFCC feature extraction — the firmware half of the edge-AI feature
// CONTRACT. This must reproduce, within tolerance, the Python reference in
//   textochip-ml/src/textochip_ml/features/mfcc.py
// pinned by the shared golden vectors
//   textochip-ml/tests/golden/mfcc_golden.json
// A training/inference feature mismatch is the #1 TinyML footgun; this file +
// host/test_ai_features.cpp turn it into a build-time check.
//
// Portable C-style (only <math.h>): the same code runs on the host, the ESP32,
// and the nRF54L. No heap, no STL — the caller provides the output buffer.
#ifndef TEXTOCHIP_AI_FEATURES_H
#define TEXTOCHIP_AI_FEATURES_H

#ifdef __cplusplus
extern "C" {
#endif

// Every knob that affects the feature numbers. Mirrors FeatureParams in Python;
// the values ship in each model's feature_params.json. Defaults = a 1 s, 16 kHz
// keyword window (n_frames = 49, n_mfcc = 13).
typedef struct {
  int sample_rate;
  float window_ms;
  float frame_ms;
  float hop_ms;
  int n_fft;     // must be a power of two (<= 1024)
  int n_mels;    // <= 64
  int n_mfcc;    // <= 32
  float fmin;
  float fmax;
  float pre_emphasis;
  int lifter;       // 0 = none
  float log_offset; // added before log() to avoid log(0)
} TcmlFeatureParams;

// The documented contract defaults (must equal Python FeatureParams()).
TcmlFeatureParams tcml_default_params(void);

int tcml_frame_length(const TcmlFeatureParams* p); // samples per analysis frame
int tcml_hop_length(const TcmlFeatureParams* p);
int tcml_window_samples(const TcmlFeatureParams* p);
int tcml_n_frames(const TcmlFeatureParams* p);

// Compute MFCC of `signal` (n_samples floats, ~ -1..1). Writes n_frames * n_mfcc
// floats to `out` (row-major, [frame][coeff]). Returns the number of frames
// written, or 0 if the signal is shorter than one frame. `out` must hold at
// least tcml_n_frames(p) * p->n_mfcc floats.
int tcml_mfcc(const float* signal, int n_samples, const TcmlFeatureParams* p,
              float* out);

#ifdef __cplusplus
}
#endif

#endif // TEXTOCHIP_AI_FEATURES_H
