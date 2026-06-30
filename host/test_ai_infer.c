// End-to-end edge-AI test (firmware side): raw audio -> features.c (MFCC) ->
// ai_infer (TFLite Micro, the model trained by textochip-ml on OUR MFCC) -> class.
// Proves the whole on-device path runs. The demo model (voice-demo-v0) tells apart
// synthetic tone-bursts: ~500 Hz = "go" (class 1), ~1500 Hz = "stop" (class 2),
// noise/quiet = "background" (class 0). Same convention as VOICE_LABELS.
//
// Build & run:  make ai-infer  (needs TFLM_DIR -> a built tflite-micro)
#include <math.h>
#include <stdio.h>

#include "../src/ai/ai.h"
#include "../src/ai/features.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// A tone burst with the same soft attack/decay envelope make_demo_model.py uses.
static void make_tone(float* sig, int n, double sr, double freq) {
  for (int i = 0; i < n; i++) {
    double t = i / sr;
    double env = t * 8.0;
    double dec = (1.0 - t) * 8.0 + 1.0;
    if (dec < env) env = dec;
    if (env < 0.0) env = 0.0;
    if (env > 1.0) env = 1.0;
    sig[i] = (float)(0.5 * sin(2.0 * M_PI * freq * t) * env);
  }
}

static int classify(double freq) {
  TcmlFeatureParams p = tcml_default_params();
  const int N = tcml_window_samples(&p);
  const int K = p.n_mfcc;
  const int nf = tcml_n_frames(&p);
  static float sig[16000];
  static float feat[49 * 13];
  make_tone(sig, N, p.sample_rate, freq);
  tcml_mfcc(sig, N, &p, feat);
  return ai_infer(feat, nf * K);
}

int main(void) {
  const int go = classify(500.0);    // -> "go"  (class 1)
  const int stop = classify(1500.0); // -> "stop" (class 2)
  printf("classes=%d  500Hz->%d (want 1=go)  1500Hz->%d (want 2=stop)\n",
         ai_num_classes(), go, stop);
  int fail = (go != 1) || (stop != 2);
  if (!fail)
    printf("OK: ai_infer end-to-end — audio -> features.c -> TFLM -> correct class\n");
  else
    printf("FAIL: unexpected class\n");
  return fail;
}
