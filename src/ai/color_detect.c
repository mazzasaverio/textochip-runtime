#include "color_detect.h"

// Vision class indices — MUST match textochip lib/missions/vision.ts VISION_LABELS
// (objects person/ball/hand = 1/2/3, then colours yellow/red/green/blue).
enum {
  CLASS_NONE = 0,
  CLASS_YELLOW = 4,
  CLASS_RED = 5,
  CLASS_GREEN = 6,
  CLASS_BLUE = 7,
  // Appended, so 1..7 keep the indices saved programs already compiled against.
  CLASS_ORANGE = 8,
  CLASS_PINK = 9,
};

// Hue bands. Orange and pink exist because their absence was a silent hole: a
// kid's orange or pink ball used to read "nothing", with no way to tell that
// from "the camera is covered". The obvious cheaper fix — widening RED to
// swallow orange — was tried and REJECTED on real photos: it relabels gold,
// amber and warm wood as red, which is worse than saying nothing. Violet
// (255-285 deg) is still uncovered on purpose: next to pink it would be an
// unstable pair on a noisy sensor, so it waits for the real camera.

// A pixel counts for a colour only if it is saturated + bright enough (so greys,
// shadows and washed-out light don't register). COVER_MIN is only a NOISE FLOOR:
// below it the "blob" is a handful of stray pixels, not an object. It used to be
// 15%, which silently hid anything not already close — and that is exactly the
// judgement the program should make, now that it can (SEESIZE() reports the
// coverage, so "stop when it fills 45% of the view" is one line of BASIC).
// Relaxed 2026-08-02, on bench evidence rather than photographs: a maker with
// real markers reported that lighter and darker shades of yellow and green were
// simply not seen. 0.40 saturation rejects a pastel; 0.20 value rejects a dark
// green under indoor light. Both floors still exist — they are what keeps grey
// walls and shadows from reading as colour — but they were set against studio
// images, and the sensor is on the desk now.
#define SAT_MIN 0.28f
#define VAL_MIN 0.13f
#define COVER_MIN 0.02f  // 2% of the pixels — noise floor, not a "close enough" test

tc_color_blob tc_detect_color_blob(const uint8_t *rgb, int width, int height) {
  tc_color_blob out = {CLASS_NONE, 0, 0};
  if (rgb == 0 || width <= 0 || height <= 0) return out;
  const int n_pixels = width * height;

  long cnt[6] = {0, 0, 0, 0, 0, 0};   // yellow, red, green, blue, orange, pink
  long sumx[6] = {0, 0, 0, 0, 0, 0};  // and their summed column, for the centroid
  for (int i = 0; i < n_pixels; i++) {
    float r = rgb[i * 3 + 0] / 255.0f;
    float g = rgb[i * 3 + 1] / 255.0f;
    float b = rgb[i * 3 + 2] / 255.0f;

    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float d = mx - mn;
    if (mx < VAL_MIN) continue;           // too dark
    if (d < SAT_MIN * mx) continue;       // too grey (S = d/mx below threshold)

    // Hue in degrees (standard RGB->H), no fmod needed: the terms stay in range.
    float h;
    if (mx == r)
      h = 60.0f * ((g - b) / d);
    else if (mx == g)
      h = 60.0f * ((b - r) / d + 2.0f);
    else
      h = 60.0f * ((r - g) / d + 4.0f);
    if (h < 0.0f) h += 360.0f;

    int c;
    // The bands are CONTIGUOUS. They used to leave gaps — 75..80 between yellow
    // and green, 165..190 between green and blue — and a hue landing in one was
    // discarded silently, which is exactly what a yellow-green marker does. A
    // borderline colour should be classified as the nearer of two neighbours,
    // never dropped. Violet (255..285) stays uncovered on purpose: beside pink
    // it is an unstable pair on a noisy sensor, and saying nothing beats saying
    // the wrong one.
    if (h < 20.0f || h >= 345.0f)
      c = 1;  // red (wraps around 0/360)
    else if (h >= 40.0f && h < 80.0f)
      c = 0;  // yellow (now meets green)
    else if (h >= 80.0f && h < 175.0f)
      c = 2;  // green (now meets blue)
    else if (h >= 175.0f && h < 255.0f)
      c = 3;  // blue (takes the old 165..190 no-man's-land)
    else if (h >= 20.0f && h < 40.0f)
      c = 4;  // orange
    else if (h >= 285.0f && h < 345.0f)
      c = 5;  // pink (through magenta)
    else
      continue;  // violet (255..285) — deliberately uncovered, see above
    cnt[c]++;
    sumx[c] += i % width;
  }

  int best = -1;
  long bestN = 0;
  for (int c = 0; c < 6; c++)
    if (cnt[c] > bestN) {
      bestN = cnt[c];
      best = c;
    }
  if (best < 0 || (float)bestN < COVER_MIN * (float)n_pixels) return out;

  static const int cls[6] = {CLASS_YELLOW, CLASS_RED, CLASS_GREEN, CLASS_BLUE,
                             CLASS_ORANGE, CLASS_PINK};
  out.cls = cls[best];
  // Centroid column -> 0..100. A one-pixel-wide frame has no left or right, so
  // it reads as centred rather than dividing by zero.
  out.x = width > 1 ? (int)((sumx[best] / bestN) * 100 / (width - 1)) : 50;
  // Coverage -> 0..100. Anything that passed the noise floor is at least 1, so
  // "size 0" keeps meaning "nothing seen" (the product's honest gate).
  out.size = (int)((bestN * 100 + n_pixels / 2) / n_pixels);
  if (out.size < 1) out.size = 1;
  if (out.size > 100) out.size = 100;
  return out;
}

int tc_detect_color(const uint8_t *rgb, int n_pixels) {
  // WHAT is in view does not depend on the frame's geometry, so a caller that
  // only wants the class need not know the width.
  return tc_detect_color_blob(rgb, n_pixels, 1).cls;
}

void tc_color_stats(const unsigned char* rgb, int width, int height, long* too_dark,
                    long* too_grey, long* no_band, long* counted) {
  long dark = 0, grey = 0, nob = 0, ok = 0;
  if (rgb && width > 0 && height > 0) {
    const int n_pixels = width * height;
    for (int i = 0; i < n_pixels; i++) {
      float r = rgb[i * 3 + 0] / 255.0f;
      float g = rgb[i * 3 + 1] / 255.0f;
      float b = rgb[i * 3 + 2] / 255.0f;
      float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
      float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
      float d = mx - mn;
      if (mx < VAL_MIN) {
        dark++;
        continue;
      }
      if (d < SAT_MIN * mx) {
        grey++;
        continue;
      }
      float h;
      if (mx == r)
        h = 60.0f * ((g - b) / d);
      else if (mx == g)
        h = 60.0f * ((b - r) / d + 2.0f);
      else
        h = 60.0f * ((r - g) / d + 4.0f);
      if (h < 0.0f) h += 360.0f;
      if (h >= 255.0f && h < 285.0f)
        nob++;  // violet, the one deliberate gap
      else
        ok++;
    }
  }
  if (too_dark) *too_dark = dark;
  if (too_grey) *too_grey = grey;
  if (no_band) *no_band = nob;
  if (counted) *counted = ok;
}

