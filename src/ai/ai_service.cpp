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

float g_signal[kMaxSamples];  // the rolling window, float ~ -1..1 (raw mic units)
float g_proc[kMaxSamples];    // DC-removed + gained copy — the actual MFCC input
float g_feat[kMaxFeat];       // MFCC output (n_frames * n_mfcc)
int16_t g_scratch[kChunk];    // small drain buffer for hal::aiCapture

// Bench/tuning view of the latest completed inference: the UN-gated argmax and
// its confidence (what the model actually thought), regardless of kMinConfidence.
int g_lastTop = -1;
float g_lastConf = 0.0f;
// Mean-abs level of the last analysed window, in int16 units (0 = silence/dead
// mic; speech runs thousands). Lets the serial heartbeat separate "mic is dead"
// from "hears audio but classifies it as background".
int g_lastLevel = 0;
// Timing of the last inference (ms): feature extraction vs model invoke — the
// bench view that caught the soft-float MFCC (doubles on an FPU-less-for-double
// M33) taking ~seconds. See the mic heartbeat in runtime.cpp.
int g_lastMfccMs = 0;
int g_lastInferMs = 0;
// AGC gain applied to the last analysed window (x10 fixed point for the log).
int g_lastGainX10 = 10;
// Envelope (max 10 ms block mean-abs, x1000) of the last window — the loudness
// the AGC keyed on; lets the bench tune the silence gate + target empirically.
int g_lastEnvX1000 = 0;
// De-spike state: the median-5 carry (previous four raw samples) + how many
// samples the median replaced by > 0.1 FS since the last inference (a direct
// read of how noisy the physical pin contact is).
int16_t g_medh[4] = {0, 0, 0, 0};
int g_spikes = 0;
int g_lastSpikes = 0;

// ── Input conditioning: high-pass + envelope AGC (bench root-cause,
// 2026-07-15). The training clips (make_voice_model.py) are Piper TTS speech
// placed at gain 0.4–1.0 of full scale over a 0.004–0.03 noise floor; a real
// INMP441 delivers speech around 0.01–0.06 FS — INSIDE the training noise band,
// so the model correctly answered "background" to every real spoken word
// (bench: background 97%→84% dip on a shout, never a word). Three stages:
//   1. one-pole HIGH-PASS (~13 Hz): kills the mic's DC pedestal AND its slow
//      wander (mean-subtraction alone left ±0.1 FS of drift in the window,
//      which capped a peak-based gain at ~3x — bench-measured). Content below
//      20 Hz never reaches the features anyway (mel fmin = 20 Hz).
//   2. envelope AGC: gain = target / max(10 ms mean-abs) — block mean-abs is
//      immune to the isolated rail spikes a marginal pin contact injects
//      (a single-sample spike would cap a raw-peak gain).
//   3. silence gate: below kAgcSilenceEnv the window stays untouched, so a
//      quiet room is NOT amplified into fake loud noise.
// In-band audio (the host tests' TTS clips) passes through unchanged; only
// quieter-than-band windows gain UP and louder-than-band windows (close
// shouting, contact crackle riding on speech) attenuate DOWN into the band.
constexpr float kAgcTargetEnv = 0.15f;   // Piper speech env at training gains
constexpr float kAgcMaxGain = 60.0f;     // cap
constexpr float kAgcSilenceEnv = 0.005f; // below this = silence, leave it alone
constexpr float kAgcHighEnv = 0.25f;     // above this = louder than training — attenuate
constexpr float kHpfPole = 0.995f;       // ~13 Hz @ 16 kHz
constexpr int kEnvBlock = 160;           // 10 ms @ 16 kHz

}  // namespace

void ai_service::lastTop(int* cls, float* conf) {
  if (cls) *cls = g_lastTop;
  if (conf) *conf = g_lastConf;
}

void ai_service::lastTiming(int* mfccMs, int* inferMs) {
  if (mfccMs) *mfccMs = g_lastMfccMs;
  if (inferMs) *inferMs = g_lastInferMs;
}

int ai_service::lastLevel() { return g_lastLevel; }

int ai_service::lastGainX10() { return g_lastGainX10; }

int ai_service::lastEnvX1000() { return g_lastEnvX1000; }

int ai_service::lastSpikes() { return g_lastSpikes; }

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
  //    DE-SPIKE with a 5-sample median as it lands: a marginal (unsoldered) pin
  //    contact injects MSB glitches — excursions of ±0.1..1.0 FS, up to a couple
  //    of samples wide — that survive a high-pass (they ARE high frequency) and
  //    were keeping the idle envelope above the AGC target (bench: env=0.159 in
  //    a quiet room -> gain stuck at 1.0; median-3 cleaned idle but speech-time
  //    vibration bursts got through). Two samples of latency; replacements are
  //    counted for the heartbeat (spk=).
  int room = g_win - g_have;
  if (room > 0) {
    int want = room < kChunk ? room : kChunk;
    int got = hal::aiCapture(g_scratch, want);
    for (int i = 0; i < got; i++) {
      // median-5 over (h0..h3, incoming) replaces the CENTER sample h2 —
      // kills glitch bursts up to 2 samples wide (median-3 only handled 1).
      int16_t v[5] = {g_medh[0], g_medh[1], g_medh[2], g_medh[3], g_scratch[i]};
      int16_t center = v[2];
      // 5-element sorting network (partial — enough to place the median)
      int16_t t;
#define TC_SWP(x, y)   if (v[x] > v[y]) { t = v[x]; v[x] = v[y]; v[y] = t; }
      TC_SWP(0, 1) TC_SWP(3, 4) TC_SWP(0, 3) TC_SWP(1, 4) TC_SWP(1, 2) TC_SWP(2, 3) TC_SWP(1, 2)
#undef TC_SWP
      int16_t m = v[2];
      int d = (int)m - (int)center;
      if (d > 3277 || d < -3277) g_spikes++;  // replaced by > 0.1 FS
      g_signal[g_have + i] = (float)m / 32768.0f;
      g_medh[0] = g_medh[1];
      g_medh[1] = g_medh[2];
      g_medh[2] = g_medh[3];
      g_medh[3] = g_scratch[i];
    }
    g_have += got;
  }

  // 2. Not a full window yet -> nothing new to report.
  if (g_have < g_win) return -1;

  // 3. Full window: extract MFCC and classify (confidence-gated — see kMinConfidence).
  {
    float sumabs = 0.0f;
    for (int i = 0; i < g_win; i++) sumabs += g_signal[i] < 0 ? -g_signal[i] : g_signal[i];
    g_lastLevel = (int)(sumabs / (float)g_win * 32768.0f);
  }
  // Condition a COPY of the window (g_proc): high-pass + envelope AGC (see the
  // constants above). Never in place — the window SLIDES, and re-gaining the
  // kept 3/4 next round would compound the gain.
  {
    // 1. one-pole HPF: y[n] = x[n] - x[n-1] + pole*y[n-1]
    float py = 0.0f;
    g_proc[0] = 0.0f;
    for (int i = 1; i < g_win; i++) {
      py = g_signal[i] - g_signal[i - 1] + kHpfPole * py;
      g_proc[i] = py;
    }
    // 2. envelope: max of 10 ms block mean-abs (spike-immune loudness estimate)
    float envMax = 0.0f;
    for (int b = 0; b + kEnvBlock <= g_win; b += kEnvBlock) {
      float m = 0.0f;
      for (int i = b; i < b + kEnvBlock; i++) m += g_proc[i] < 0 ? -g_proc[i] : g_proc[i];
      m /= (float)kEnvBlock;
      if (m > envMax) envMax = m;
    }
    g_lastEnvX1000 = (int)(envMax * 1000.0f + 0.5f);
    // 3. gain (silence-gated, capped) + clip. Quiet speech is amplified UP to
    //    the training band; OVER-loud windows (close shouting, or contact
    //    crackle riding on speech — bench: env=0.536 vs a 0.10-0.25 training
    //    band) are attenuated DOWN to the band's top so the model never sees
    //    energies it never trained on. In-band audio (the host tests' TTS
    //    clips) still passes untouched.
    float gain = 1.0f;
    if (envMax > kAgcSilenceEnv && envMax < kAgcTargetEnv) {
      gain = kAgcTargetEnv / envMax;
      if (gain > kAgcMaxGain) gain = kAgcMaxGain;
    } else if (envMax > kAgcHighEnv) {
      gain = kAgcHighEnv / envMax;  // attenuate into the band (gain < 1)
    }
    g_lastGainX10 = (int)(gain * 10.0f + 0.5f);
    if (gain != 1.0f) {
      for (int i = 0; i < g_win; i++) {
        float v = g_proc[i] * gain;
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        g_proc[i] = v;
      }
    }
  }
  g_lastSpikes = g_spikes;
  g_spikes = 0;
  uint32_t t0 = hal::nowMs();
  int nf = tcml_mfcc(g_proc, g_win, &g_params, g_feat);
  uint32_t t1 = hal::nowMs();
  g_lastMfccMs = (int)(t1 - t0);
  int cls = 0;
  if (nf > 0) {
    float conf = 0.0f;
    int top = ai_infer_top(g_feat, nf * g_params.n_mfcc, &conf);
    g_lastInferMs = (int)(hal::nowMs() - t1);
    g_lastTop = top;
    g_lastConf = conf;
    cls = (top > 0 && conf >= kMinConfidence) ? top : 0;
  } else {
    g_lastTop = -1;
    g_lastConf = 0.0f;
  }

  // 4. Slide the window forward by one hop (keep the newest g_win - g_hop samples),
  //    so the next inference overlaps this one — a keyword that lands across a
  //    window boundary still gets a clean look on the following window.
  int keep = g_win - g_hop;
  memmove(g_signal, g_signal + g_hop, (size_t)keep * sizeof(float));
  g_have = keep;

  return cls < 0 ? 0 : cls;
}
