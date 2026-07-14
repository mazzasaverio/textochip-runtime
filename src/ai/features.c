// MFCC feature extraction — the on-device mirror of textochip-ml's Python MFCC.
// See features.h. Plain C + <math.h>; double internally for parity with NumPy,
// float output. Fixed upper bounds keep it heap-free for the MCU.
//
// SCRATCH IS STATIC, NOT STACK (bench lesson, nRF54LM20 DK 2026-07-14): at the
// old caps the locals (win + fb + dct + re/im) added up to ~300 KB of stack and
// the FIRST on-device inference blew the 8 KB main-thread stack with a usage
// fault — host builds never see it (MB-sized stacks). The buffers are now
// `static` (this makes tcml_mfcc NON-REENTRANT — fine: the one on-device caller
// is the ai_service on the main loop, and host tests call it sequentially), and
// the caps are the shipped model's envelope (n_fft 512, 40 mels, 16 MFCC,
// 30 ms frame = 480 samples), giving ~100 KB of .bss. The MATH is untouched:
// storage class and bounds only, so the Python golden-vector parity holds.
#include "features.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAX_NFFT 512
#define MAX_NMELS 40
#define MAX_NMFCC 16
#define MAX_FRAME 512

TcmlFeatureParams tcml_default_params(void) {
  TcmlFeatureParams p;
  p.sample_rate = 16000;
  p.window_ms = 1000.0f;
  p.frame_ms = 30.0f;
  p.hop_ms = 20.0f;
  p.n_fft = 512;
  p.n_mels = 40;
  p.n_mfcc = 13;
  p.fmin = 20.0f;
  p.fmax = 8000.0f;
  p.pre_emphasis = 0.97f;
  p.lifter = 0;
  p.log_offset = 1e-6f;
  return p;
}

static int iround(double x) { return (int)floor(x + 0.5); }

int tcml_frame_length(const TcmlFeatureParams* p) {
  return iround(p->sample_rate * (double)p->frame_ms / 1000.0);
}
int tcml_hop_length(const TcmlFeatureParams* p) {
  return iround(p->sample_rate * (double)p->hop_ms / 1000.0);
}
int tcml_window_samples(const TcmlFeatureParams* p) {
  return iround(p->sample_rate * (double)p->window_ms / 1000.0);
}
int tcml_n_frames(const TcmlFeatureParams* p) {
  int fl = tcml_frame_length(p), hop = tcml_hop_length(p);
  int ws = tcml_window_samples(p);
  if (ws < fl) return 0;
  return 1 + (ws - fl) / hop;
}

// In-place iterative radix-2 Cooley-Tukey FFT (n a power of two). re/im length n.
static void fft(double* re, double* im, int n) {
  // bit-reversal permutation
  for (int i = 1, j = 0; i < n; i++) {
    int bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) {
      double tr = re[i]; re[i] = re[j]; re[j] = tr;
      double ti = im[i]; im[i] = im[j]; im[j] = ti;
    }
  }
  for (int len = 2; len <= n; len <<= 1) {
    double ang = -2.0 * M_PI / len;
    double wlr = cos(ang), wli = sin(ang);
    for (int i = 0; i < n; i += len) {
      double wr = 1.0, wi = 0.0;
      for (int k = 0; k < len / 2; k++) {
        double ur = re[i + k], ui = im[i + k];
        double vr = re[i + k + len / 2] * wr - im[i + k + len / 2] * wi;
        double vi = re[i + k + len / 2] * wi + im[i + k + len / 2] * wr;
        re[i + k] = ur + vr; im[i + k] = ui + vi;
        re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
        double nwr = wr * wlr - wi * wli;
        wi = wr * wli + wi * wlr; wr = nwr;
      }
    }
  }
}

static double hz_to_mel(double hz) { return 2595.0 * log10(1.0 + hz / 700.0); }
static double mel_to_hz(double mel) { return 700.0 * (pow(10.0, mel / 2595.0) - 1.0); }

// Triangular mel filterbank into fb[n_mels][n_bins]. Matches Python mel_filterbank.
static void build_filterbank(const TcmlFeatureParams* p, double* fb, int n_bins) {
  int M = p->n_mels;
  double mmin = hz_to_mel(p->fmin), mmax = hz_to_mel(p->fmax);
  int bin[MAX_NMELS + 2];
  for (int i = 0; i < M + 2; i++) {
    double mel = mmin + (mmax - mmin) * i / (double)(M + 1);
    double hz = mel_to_hz(mel);
    int b = (int)floor((p->n_fft + 1) * hz / p->sample_rate);
    if (b < 0) b = 0;
    if (b > n_bins - 1) b = n_bins - 1;
    bin[i] = b;
  }
  memset(fb, 0, (size_t)M * n_bins * sizeof(double));
  for (int m = 1; m <= M; m++) {
    int left = bin[m - 1], center = bin[m], right = bin[m + 1];
    for (int k = left; k < center; k++)
      if (center > left) fb[(m - 1) * n_bins + k] = (k - left) / (double)(center - left);
    for (int k = center; k < right; k++)
      if (right > center) fb[(m - 1) * n_bins + k] = (right - k) / (double)(right - center);
  }
}

int tcml_mfcc(const float* signal, int n_samples, const TcmlFeatureParams* p,
              float* out) {
  const int fl = tcml_frame_length(p);
  const int hop = tcml_hop_length(p);
  const int nfft = p->n_fft;
  const int n_bins = nfft / 2 + 1;
  const int M = p->n_mels;
  const int K = p->n_mfcc;
  if (fl > MAX_FRAME || nfft > MAX_NFFT || M > MAX_NMELS || K > MAX_NMFCC) return 0;
  if (n_samples < fl) return 0;
  const int n_frames = 1 + (n_samples - fl) / hop;

  // Hamming window (NumPy: 0.54 - 0.46 cos(2*pi*n/(N-1))).
  // static: see the file header — these five buffers used to be ~300 KB of stack.
  static double win[MAX_FRAME];
  for (int n = 0; n < fl; n++)
    win[n] = 0.54 - 0.46 * cos(2.0 * M_PI * n / (double)(fl - 1));

  static double fb[MAX_NMELS * (MAX_NFFT / 2 + 1)];
  build_filterbank(p, fb, n_bins);

  // Orthonormal DCT-II basis (K x M).
  static double dct[MAX_NMFCC * MAX_NMELS];
  for (int k = 0; k < K; k++) {
    double scale = sqrt(2.0 / M) * (k == 0 ? 1.0 / sqrt(2.0) : 1.0);
    for (int m = 0; m < M; m++)
      dct[k * M + m] = cos(M_PI * (2 * m + 1) * k / (2.0 * M)) * scale;
  }

  static double re[MAX_NFFT], im[MAX_NFFT];
  for (int f = 0; f < n_frames; f++) {
    const int start = f * hop;
    // pre-emphasis (applied across the signal) + window, into the FFT buffer.
    for (int n = 0; n < nfft; n++) { re[n] = 0.0; im[n] = 0.0; }
    for (int n = 0; n < fl; n++) {
      int idx = start + n;
      double x = signal[idx];
      double xm1 = (idx > 0) ? signal[idx - 1] : 0.0;
      double emph = (p->pre_emphasis > 0.0f) ? (x - p->pre_emphasis * xm1) : x;
      re[n] = emph * win[n];
    }
    fft(re, im, nfft);
    // mel energies -> log -> DCT
    double logmel[MAX_NMELS];
    for (int m = 0; m < M; m++) {
      double e = 0.0;
      for (int k = 0; k < n_bins; k++) {
        double power = (re[k] * re[k] + im[k] * im[k]) / (double)nfft;
        e += power * fb[m * n_bins + k];
      }
      logmel[m] = log(e + p->log_offset);
    }
    for (int k = 0; k < K; k++) {
      double c = 0.0;
      for (int m = 0; m < M; m++) c += logmel[m] * dct[k * M + m];
      if (p->lifter > 0)
        c *= 1.0 + (p->lifter / 2.0) * sin(M_PI * k / (double)p->lifter);
      out[f * K + k] = (float)c;
    }
  }
  return n_frames;
}
