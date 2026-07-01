// Edge-AI SERVICE end-to-end (firmware side): real synthetic SPEECH -> hal mic stub
// -> ai_service (features.c MFCC -> ai_infer/TFLM) -> class, then the runtime driving
// the service so the configurator's VOICE() program moves the motors. Proves the
// whole on-device path with the REAL trained model and NO hardware. It exercises the
// new pieces: hal::aiCapture, ai_service, and INFER auto-starting the service (the
// product emits no AISTART). Build & run:  make test-ai-service
#include <cstdint>
#include <cstdio>

#include "../src/ai/ai.h"
#include "../src/ai/ai_service.h"
#include "../src/ai/features.h"
#include "../src/ai/models/voice/voice_test_samples.h"
#include "../src/runtime.h"
#include "hal_host.h"

// Build a REPRESENTATIVE 1 s capture as int16 PCM — the word over a low noise floor
// at an offset, the way the mic sees it (a clean zero-padded word is out of
// distribution). Same recipe as test_ai_infer.c, then quantized to int16 for the
// mic stub (the service converts back to float, a ~1/32768 round-trip). Fixed-seed
// noise -> deterministic.
static int build_window_i16(const float* word, int word_n, int16_t* out) {
  TcmlFeatureParams p = tcml_default_params();
  const int N = tcml_window_samples(&p);
  const int offset = 3000;
  static float win[16000];
  unsigned int seed = 12345u;
  for (int i = 0; i < N; i++) {
    seed = seed * 1103515245u + 12345u;
    win[i] = (((float)((seed >> 16) & 0x7fff) / 32768.0f) - 0.5f) * 0.02f;
  }
  for (int i = 0; i < word_n && offset + i < N; i++) win[offset + i] += word[i];
  for (int i = 0; i < N; i++) {
    float v = win[i] > 1.0f ? 1.0f : (win[i] < -1.0f ? -1.0f : win[i]);
    out[i] = (int16_t)(v * 32767.0f);
  }
  return N;
}

// Feed one word's window through the service (mic stub -> features -> ai_infer) and
// return the class it reports once the window fills.
static int classify_via_service(const float* word, int word_n) {
  static int16_t pcm[16000];
  int n = build_window_i16(word, word_n, pcm);
  host_reset_audio();
  host_feed_audio(pcm, n);
  ai_service::reset();
  int cls = -1;
  for (int i = 0; i < 300 && cls < 0; i++) cls = ai_service::poll();
  return cls;
}

int main(void) {
  struct {
    const char* word;
    const float* pcm;
    int n;
    int want;
  } cases[] = {
      {"go", go_pcm, GO_N, 1},
      {"left", left_pcm, LEFT_N, 2},
      {"right", right_pcm, RIGHT_N, 3},
      {"stop", stop_pcm, STOP_N, 4},
  };
  int fail = 0;

  // 1) The service classifies each keyword from audio (the new capture path).
  printf("service: mic stub -> features.c -> ai_infer (TFLM)\n");
  for (const auto& c : cases) {
    int got = classify_via_service(c.pcm, c.n);
    const bool ok = (got == c.want);
    printf("  \"%s\" -> %d (want %d)%s\n", c.word, got, c.want,
           ok ? "" : "   <-- MISMATCH");
    if (!ok) fail = 1;
  }
  // Silence / noise must NOT fire a command — the confidence gate reports 0 (none),
  // so the robot doesn't twitch on ambient sound at the bench.
  {
    int got = classify_via_service(go_pcm, 0);  // 0 word samples = just the noise floor
    printf("  (noise) -> %d (want 0 = none)%s\n", got, got == 0 ? "" : "   <-- MISMATCH");
    if (got != 0) fail = 1;
  }

  // 2) TRUE end-to-end: "go" audio -> the runtime drives the service -> the
  //    configurator's voice program reads VOICE() and drives the motors. Proves the
  //    whole board path, incl. INFER auto-starting the service (no AISTART emitted).
  static const char* PROGRAM[] = {
      "INFER voice", "PUSH 1", "EQ", "JZ 5",  "MOVE 160 160",
      "INFER voice", "PUSH 4", "EQ", "JZ 10", "MOVE 0 0",
      "INFER voice", "PUSH 2", "EQ", "JZ 15", "MOVE 40 160",
      "INFER voice", "PUSH 3", "EQ", "JZ 20", "MOVE 160 40",
      "JMP 0",
  };
  static int16_t pcm[16000];
  int n = build_window_i16(go_pcm, GO_N, pcm);
  host_reset_audio();
  host_feed_audio(pcm, n);
  // Drive the protocol directly (no runtime::init(), so a saved autorun file can't
  // interfere): LOAD the program, RUN, then tick until it drives forward.
  runtime::feedLine("LOAD");
  for (const char* line : PROGRAM) runtime::feedLine(line);
  runtime::feedLine(".");
  runtime::feedLine("RUN");
  int l = 0, r = 0;
  bool moved = false;
  for (int i = 0; i < 600 && !(moved && l == 160 && r == 160); i++) {
    runtime::tick();
    moved = host_get_move(&l, &r);
  }
  const bool e2e_ok = moved && l == 160 && r == 160;
  printf("end-to-end: heard \"go\" -> MOVE %d %d (want 160 160)%s\n", l, r,
         e2e_ok ? "" : "   <-- MISMATCH");
  if (!e2e_ok) fail = 1;

  if (!fail)
    printf(
        "OK: edge-AI service — TTS speech -> features.c -> TFLM -> class -> the "
        "voice program drives the motors\n");
  return fail;
}
