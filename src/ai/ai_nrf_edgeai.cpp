// Edge-AI backend #2: Nordic's nRF Edge AI Library on the Axon NPU.
//
// This is the ALTERNATE implementation of the ai_service:: interface (the other
// is ai_service.cpp: our MFCC + TFLite Micro path). CMake compiles exactly one,
// selected by CONFIG_TEXTOCHIP_NRF_EDGEAI (the nRF54LM20B build), so runtime.cpp
// and the VOICE()/INFER opcodes are byte-for-byte unchanged.
//
// Nordic's model is STREAMING and END-TO-END: fed 160 raw int16 samples (10 ms)
// at a time, it keeps its own mel-spectrogram history internally and emits a
// 12-class posterior every block, accelerated by the Axon NPU. We feed it the
// SAME 16 kHz mono audio our TDM/INMP441 aiCapture already produces (no PDM
// required), map its keyword classes onto ours (go/left/right/stop), and run
// Nordic's own tuned post-processor (EMA + per-class threshold + count-in-row +
// lockout — values copied from applications/ww_kws) to decide a detection.
//
// The lib + Axon driver + the compiled model come from the sdk-edge-ai add-on
// (proprietary, referenced via the build's EXTRA_ZEPHYR_MODULES — see the board
// .conf and zephyr/CMakeLists.txt). This .cpp and our repo contain none of it.
#include "ai_service.h"

#include "hal.h"

#ifdef TEXTOCHIP_NRF_EDGEAI

extern "C" {
#include <nrf_edgeai/nrf_edgeai.h>

#include "nrf_edgeai_user_model.h"         // nrf_edgeai_user_model() (the getter macro)
#include "nrf_edgeai_user_model_labels.h"  // NRF_EDGEAI_USER_LABELS_NAME[] strings
}

namespace {

constexpr int kBlock = 160;  // Nordic KWS window = 160 samples = 10 ms @ 16 kHz

// Nordic's tuned KWS post-processing (applications/ww_kws/src/kws): EMA of the
// predicted class probability, fire when it stays >= threshold for num_in_row
// consecutive blocks, then a negative-count lockout to stop double-spotting.
constexpr float kEmaAlpha = 0.12f;   // CONFIG_KWS_EMA_ALPHA default (120/1000)
constexpr float kThreshold = 0.8f;   // per-keyword threshold (all keywords)
constexpr int kNumInRow = 10;        // consecutive blocks above threshold
constexpr int kSkipBlocks = 10;      // lockout blocks after a fire
// Throttle the "no detection" heartbeat: poll runs per 10 ms block (~100/s), but
// the serial heartbeat + VOID-class updates should stay ~4/s like the TFLM path.
constexpr int kIdleReportEvery = 25;

// Case-insensitive equality of two label words (no locale, ASCII only — the
// labels are short command names).
bool label_eq(const char* a, const char* b) {
  if (a == nullptr || b == nullptr) return false;
  for (; *a && *b; ++a, ++b) {
    char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
    char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
    if (ca != cb) return false;
  }
  return *a == '\0' && *b == '\0';
}

// Map a predicted class onto ours (0=none/background, 1=go, 2=left, 3=right,
// 4=stop — the VOICE_LABELS contract) by the label's NAME, not its enum index.
// Every generated model ships NRF_EDGEAI_USER_LABELS_NAME[]; matching on the
// word (English OR Italian) means a custom Edge AI Lab model drops into this
// build with no code change here — retrain the KWS model with vai/sinistra/
// destra/fermo (or add a "hey chip" wake word) and this still maps it. Any
// unrecognised class (silence/other/an unmapped keyword/a wake word) -> 0.
int our_class(uint16_t predicted) {
  constexpr int kNames =
      (int)(sizeof(NRF_EDGEAI_USER_LABELS_NAME) / sizeof(NRF_EDGEAI_USER_LABELS_NAME[0]));
  if ((int)predicted >= kNames) return 0;
  const char* name = NRF_EDGEAI_USER_LABELS_NAME[predicted];
  struct Syn {
    const char* word;
    int cls;
  };
  static const Syn kSyn[] = {
      {"go", 1},   {"vai", 1},      {"avanti", 1}, {"forward", 1},
      {"left", 2}, {"sinistra", 2}, {"right", 3},  {"destra", 3},
      {"stop", 4}, {"fermo", 4},    {"ferma", 4},  {"alt", 4},   {"halt", 4},
  };
  for (const Syn& s : kSyn)
    if (label_eq(name, s.word)) return s.cls;
  return 0;
}

nrf_edgeai_t* g_model = nullptr;
bool g_started = false;

int16_t g_block[kBlock];  // the 160-sample accumulator
int g_fill = 0;           // samples currently in g_block
int16_t g_scratch[256];   // drained from aiCapture per poll

// Nordic post-proc state
uint16_t g_lastClass = 0;
int g_count = 0;
float g_ema = 0.0f;

// Bench/heartbeat mirror of the last processed block
int g_level = 0;      // mean-abs of the last block (int16 units)
int g_topOur = 0;     // our-mapped predicted class of the last block
float g_topConf = 0;  // its EMA confidence
int g_inferMs = 0;    // Axon inference time of the last block
int g_idle = 0;       // idle-report throttle counter

// Process one full 160-sample block. Returns our-mapped class on a FIRE, else 0.
int process_block() {
  long sumabs = 0;
  for (int i = 0; i < kBlock; i++) sumabs += g_block[i] < 0 ? -g_block[i] : g_block[i];
  g_level = (int)(sumabs / kBlock);

  uint32_t t0 = hal::nowMs();
  nrf_edgeai_feed_inputs(g_model, g_block, (uint16_t)kBlock);
  nrf_edgeai_run_inference(g_model);
  g_inferMs = (int)(hal::nowMs() - t0);

  const uint16_t predicted = g_model->decoded_output.classif.predicted_class;
  const float prob = g_model->decoded_output.classif.probabilities.p_f32[predicted];

  // Nordic's kws_postprocess, verbatim in spirit. Any class that isn't one of
  // our four commands (silence/other/background, or an unmapped keyword) resets
  // the smoother — expressed via our_class so it needs no model-specific enum
  // names (and so a loud non-command word can't trip the fire lockout).
  if (our_class(predicted) == 0) {
    g_count = 0;
    g_ema = 0.0f;
    g_topOur = 0;
    g_topConf = 0.0f;
    return 0;
  }
  if (predicted != g_lastClass) {
    g_lastClass = predicted;
    g_count = 0;
    g_ema = 0.0f;
  }
  g_count++;
  g_ema = kEmaAlpha * prob + (1.0f - kEmaAlpha) * g_ema;
  g_topOur = our_class(predicted);
  g_topConf = g_ema;

  if (g_count >= kNumInRow && g_ema >= kThreshold) {
    g_count = -kSkipBlocks;  // lockout
    g_ema = 0.0f;
    return our_class(predicted);
  }
  return 0;
}

}  // namespace

void ai_service::reset() {
  if (!g_started) {
    g_model = nrf_edgeai_user_model();
    if (g_model != nullptr && nrf_edgeai_init(g_model) == NRF_EDGEAI_ERR_SUCCESS) {
      g_started = true;
    }
  }
  g_fill = 0;
  g_lastClass = 0;
  g_count = 0;
  g_ema = 0.0f;
  g_idle = 0;
}

int ai_service::poll() {
  if (!g_started) {
    reset();
    if (!g_started) return -1;  // model not available
  }

  // 1. Drain new mic audio and fold it into 160-sample blocks.
  int fired = -1;    // -1 = no block processed this call
  bool processed = false;
  int got = hal::aiCapture(g_scratch, (int)sizeof(g_scratch) / (int)sizeof(g_scratch[0]));
  for (int i = 0; i < got; i++) {
    g_block[g_fill++] = g_scratch[i];
    if (g_fill == kBlock) {
      g_fill = 0;
      int det = process_block();
      processed = true;
      if (det > 0) fired = det;          // a keyword fired
      else if (fired < 0) fired = 0;     // processed, nothing (yet)
    }
  }

  // 2. Report cadence: fire immediately; otherwise emit a "none" (which clears
  //    VOICE() and drives the heartbeat) only every kIdleReportEvery blocks, so
  //    the ~100/s block rate doesn't spam the serial log or thrash the class reg.
  if (fired > 0) {
    g_idle = 0;
    return fired;
  }
  if (processed && ++g_idle >= kIdleReportEvery) {
    g_idle = 0;
    return 0;
  }
  return -1;
}

void ai_service::lastTop(int* cls, float* conf) {
  if (cls) *cls = g_topOur;
  if (conf) *conf = g_topConf;
}

int ai_service::lastLevel() { return g_level; }

void ai_service::lastTiming(int* mfccMs, int* inferMs) {
  if (mfccMs) *mfccMs = 0;  // Nordic's model does its DSP internally (no separate MFCC)
  if (inferMs) *inferMs = g_inferMs;
}

// The Nordic model + its baked DSP handle conditioning; our AGC/de-spike/env
// stages are not on this path, so the heartbeat's gain/env/spk read neutral.
int ai_service::lastGainX10() { return 10; }
int ai_service::lastEnvX1000() { return 0; }
int ai_service::lastSpikes() { return 0; }

int ai_service::gatePct() { return (int)(kThreshold * 100.0f + 0.5f); }

bool ai_service::inRefractory() { return g_count < 0; }

#endif  // TEXTOCHIP_NRF_EDGEAI
