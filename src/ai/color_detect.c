#include "color_detect.h"

// Vision class indices — MUST match textochip lib/missions/vision.ts VISION_LABELS
// (objects person/ball/hand = 1/2/3, then colours yellow/red/green/blue).
enum {
  CLASS_NONE = 0,
  CLASS_YELLOW = 4,
  CLASS_RED = 5,
  CLASS_GREEN = 6,
  CLASS_BLUE = 7,
};

// A pixel counts for a colour only if it is saturated + bright enough (so greys,
// shadows and washed-out light don't register). COVER_MIN is only a NOISE FLOOR:
// below it the "blob" is a handful of stray pixels, not an object. It used to be
// 15%, which silently hid anything not already close — and that is exactly the
// judgement the program should make, now that it can (SEESIZE() reports the
// coverage, so "stop when it fills 45% of the view" is one line of BASIC).
#define SAT_MIN 0.40f
#define VAL_MIN 0.20f
#define COVER_MIN 0.02f  // 2% of the pixels — noise floor, not a "close enough" test

tc_color_blob tc_detect_color_blob(const uint8_t *rgb, int width, int height) {
  tc_color_blob out = {CLASS_NONE, 0, 0};
  if (rgb == 0 || width <= 0 || height <= 0) return out;
  const int n_pixels = width * height;

  long cnt[4] = {0, 0, 0, 0};   // pixels per colour: yellow, red, green, blue
  long sumx[4] = {0, 0, 0, 0};  // and their summed column, for the centroid
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
    if (h < 20.0f || h >= 345.0f)
      c = 1;  // red (wraps around 0/360)
    else if (h >= 40.0f && h < 75.0f)
      c = 0;  // yellow
    else if (h >= 80.0f && h < 165.0f)
      c = 2;  // green
    else if (h >= 190.0f && h < 255.0f)
      c = 3;  // blue
    else
      continue;
    cnt[c]++;
    sumx[c] += i % width;
  }

  int best = -1;
  long bestN = 0;
  for (int c = 0; c < 4; c++)
    if (cnt[c] > bestN) {
      bestN = cnt[c];
      best = c;
    }
  if (best < 0 || (float)bestN < COVER_MIN * (float)n_pixels) return out;

  static const int cls[4] = {CLASS_YELLOW, CLASS_RED, CLASS_GREEN, CLASS_BLUE};
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
