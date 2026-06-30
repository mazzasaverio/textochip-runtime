// End-to-end edge-AI test (firmware side): real synthetic SPEECH -> features.c
// (MFCC) -> ai_infer (TFLite Micro, the model textochip-ml trained on OUR MFCC from
// Piper-TTS speech) -> class. Proves the whole on-device path on real words, no mic.
//
// The samples (voice_test_samples.h) are TTS "go" / "stop"; the model (voice-v1)
// classifies background=0, go=1, stop=2 — the VOICE_LABELS convention.
//
// Build & run:  make ai-infer  (needs TFLM_DIR -> a built tflite-micro)
#include <stdio.h>
#include <string.h>

#include "../src/ai/ai.h"
#include "../src/ai/features.h"
#include "../src/ai/models/voice/voice_test_samples.h"

// Place a raw word into a 1 s window (zero-padded) and classify it.
static int classify(const float* word, int word_n) {
  TcmlFeatureParams p = tcml_default_params();
  const int N = tcml_window_samples(&p);
  const int K = p.n_mfcc;
  const int nf = tcml_n_frames(&p);
  static float win[16000];
  static float feat[49 * 13];
  memset(win, 0, sizeof(win));
  int n = word_n < N ? word_n : N;
  memcpy(win, word, (size_t)n * sizeof(float));
  tcml_mfcc(win, N, &p, feat);
  return ai_infer(feat, nf * K);
}

int main(void) {
  const int go = classify(go_pcm, GO_N);     // -> "go"  (class 1)
  const int stop = classify(stop_pcm, STOP_N); // -> "stop" (class 2)
  printf("classes=%d  \"go\"->%d (want 1)  \"stop\"->%d (want 2)\n",
         ai_num_classes(), go, stop);
  int fail = (go != 1) || (stop != 2);
  if (!fail)
    printf("OK: ai_infer end-to-end — TTS speech -> features.c -> TFLM -> correct word\n");
  else
    printf("FAIL: unexpected class\n");
  return fail;
}
