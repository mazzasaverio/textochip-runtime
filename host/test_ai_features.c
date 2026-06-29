// Feature-parity test (firmware side). Recomputes the SAME deterministic signal
// and golden MFCC as textochip-ml/tests/golden/mfcc_golden.json, and checks the C
// extractor (src/ai/features.c) against it. If this passes AND the Python test
// passes, training and on-device features agree — the #1 TinyML footgun is closed.
//
// Build & run:  make test-ai
#include <math.h>
#include <stdio.h>

#include "../src/ai/features.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int main(void) {
  TcmlFeatureParams p = tcml_default_params();
  const int N = tcml_window_samples(&p); // 16000
  const int K = p.n_mfcc;                // 13
  static float sig[16000];
  for (int n = 0; n < N; n++) {
    double t = n / (double)p.sample_rate;
    sig[n] = (float)(0.6 * sin(2 * M_PI * 440 * t) + 0.3 * sin(2 * M_PI * 880 * t));
  }

  static float out[49 * 13];
  const int got = tcml_mfcc(sig, N, &p, out);

  // Golden per-coefficient means + sum (mirrors mfcc_golden.json, tol 0.01).
  const double golden[13] = {-69.047646, 12.809998, -0.356389, -7.806218,
                             -7.812423,  -3.368524, 0.723432,  1.127965,
                             -1.235528,  -2.860549, -1.082931, 3.149199,
                             6.312377};
  const double tol = 0.01;

  double mean[13] = {0};
  double sum = 0;
  for (int f = 0; f < got; f++)
    for (int k = 0; k < K; k++) {
      mean[k] += out[f * K + k];
      sum += out[f * K + k];
    }
  for (int k = 0; k < K; k++) mean[k] /= (got > 0 ? got : 1);

  int fail = 0;
  if (got != 49) {
    printf("FAIL: frames %d != 49\n", got);
    fail = 1;
  }
  for (int k = 0; k < K; k++) {
    double d = fabs(mean[k] - golden[k]);
    if (d > tol) {
      printf("FAIL: coeff %d  C=%.6f  golden=%.6f  |d|=%.5f\n", k, mean[k],
             golden[k], d);
      fail = 1;
    }
  }
  if (!fail)
    printf("OK: C MFCC matches the Python golden vectors "
           "(frames=%d, sum=%.4f, tol=%.3f)\n",
           got, sum, tol);
  return fail;
}
