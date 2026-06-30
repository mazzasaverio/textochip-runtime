// End-to-end edge-AI test (firmware side): real synthetic SPEECH -> features.c
// (MFCC) -> ai_infer (TFLite Micro, the model textochip-ml trained on OUR MFCC from
// Piper-TTS speech) -> class. Proves the whole on-device path on real words, no mic.
//
// The model (voice-v1) classifies background=0, go=1, left=2, right=3, stop=4 — the
// VOICE_LABELS convention. voice_test_samples.h holds held-out TTS of each word.
//
// Build & run:  make ai-infer   (TFLM_DIR defaults to the third_party submodule)
#include <stdio.h>
#include <string.h>

#include "../src/ai/ai.h"
#include "../src/ai/features.h"
#include "../src/ai/models/voice/voice_test_samples.h"

// Build a REPRESENTATIVE 1 s capture — the word over a low noise floor at an
// offset, the way the mic actually sees it (the model trains on this; a clean
// zero-padded word is out-of-distribution). Noise is a fixed LCG so the test is
// deterministic; the model is robust to the noise realisation, only the level.
static int classify(const float* word, int word_n) {
  TcmlFeatureParams p = tcml_default_params();
  const int N = tcml_window_samples(&p);
  const int K = p.n_mfcc;
  const int nf = tcml_n_frames(&p);
  const int offset = 3000;
  static float win[16000];
  static float feat[49 * 13];
  unsigned int seed = 12345u;
  for (int i = 0; i < N; i++) {
    seed = seed * 1103515245u + 12345u;
    win[i] = (((float)((seed >> 16) & 0x7fff) / 32768.0f) - 0.5f) * 0.02f;
  }
  for (int i = 0; i < word_n && offset + i < N; i++) win[offset + i] += word[i];
  tcml_mfcc(win, N, &p, feat);
  return ai_infer(feat, nf * K);
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
  printf("classes=%d\n", ai_num_classes());
  for (int i = 0; i < 4; i++) {
    int got = classify(cases[i].pcm, cases[i].n);
    printf("  \"%s\" -> %d (want %d)%s\n", cases[i].word, got, cases[i].want,
           got == cases[i].want ? "" : "   <-- MISMATCH");
    if (got != cases[i].want) fail = 1;
  }
  if (!fail)
    printf("OK: ai_infer end-to-end — TTS speech -> features.c -> TFLM -> correct word\n");
  return fail;
}
