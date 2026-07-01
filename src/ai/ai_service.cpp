#include "ai_service.h"

#include <string.h>  // memmove

#include "ai.h"
#include "features.h"
#include "hal.h"

// The background inference service (see ai_service.h). All buffers are STATIC — no
// heap on the MCU. They are sized to the default keyword window (1 s @ 16 kHz =
// 16000 samples, MFCC = 49 frames x 13 coeffs); reset() clamps the runtime window
// to these caps so an over-large feature_params can't overflow them.
namespace {

constexpr int kMaxSamples = 16000;  // 1 s @ 16 kHz (tcml_default_params)
constexpr int kMaxFeat = 49 * 13;   // n_frames * n_mfcc at the defaults
constexpr int kChunk = 256;         // samples drained per poll (~one I2S DMA block)
// Detection confidence gate: a window whose top class scores below this reports 0
// (none), so ambient noise / silence / half-words don't fire a command — the robot
// only reacts to a clearly-spoken word. (The model also has a trained "background"
// class 0; this is the second guard.) Tune up if it reacts to noise, down if clear
// words are missed.
constexpr float kMinConfidence = 0.6f;

TcmlFeatureParams g_params;
int g_win = 0;   // analysis window length (samples)
int g_hop = 0;   // slide between inferences (samples)
int g_have = 0;  // valid samples currently held in g_signal
bool g_ready = false;

float g_signal[kMaxSamples];  // the rolling window, float ~ -1..1 (the MFCC input)
float g_feat[kMaxFeat];       // MFCC output (n_frames * n_mfcc)
int16_t g_scratch[kChunk];    // small drain buffer for hal::aiCapture

}  // namespace

void ai_service::reset() {
  g_params = tcml_default_params();
  g_win = tcml_window_samples(&g_params);
  if (g_win > kMaxSamples) g_win = kMaxSamples;  // guard the static buffer
  g_hop = g_win / 4;                              // ~4 inferences/sec (0.25 s slide)
  if (g_hop < 1) g_hop = 1;
  g_have = 0;
  g_ready = true;
}

int ai_service::poll() {
  if (!g_ready) reset();

  // 1. Drain a bounded chunk of new audio into the rolling window (non-blocking).
  int room = g_win - g_have;
  if (room > 0) {
    int want = room < kChunk ? room : kChunk;
    int got = hal::aiCapture(g_scratch, want);
    for (int i = 0; i < got; i++) {
      g_signal[g_have + i] = (float)g_scratch[i] / 32768.0f;
    }
    g_have += got;
  }

  // 2. Not a full window yet -> nothing new to report.
  if (g_have < g_win) return -1;

  // 3. Full window: extract MFCC and classify (confidence-gated — see kMinConfidence).
  int nf = tcml_mfcc(g_signal, g_win, &g_params, g_feat);
  int cls =
      (nf > 0) ? ai_infer_conf(g_feat, nf * g_params.n_mfcc, kMinConfidence) : 0;

  // 4. Slide the window forward by one hop (keep the newest g_win - g_hop samples),
  //    so the next inference overlaps this one — a keyword that lands across a
  //    window boundary still gets a clean look on the following window.
  int keep = g_win - g_hop;
  memmove(g_signal, g_signal + g_hop, (size_t)keep * sizeof(float));
  g_have = keep;

  return cls < 0 ? 0 : cls;
}
