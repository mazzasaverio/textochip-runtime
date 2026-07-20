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
// shadows and washed-out light don't register). The winning colour must then
// cover at least COVER_MIN of the frame — a coloured object in front fills much
// of the view up close, background clutter at the edges does not dominate.
#define SAT_MIN 0.40f
#define VAL_MIN 0.20f
#define COVER_MIN 0.15f  // 15% of the pixels

int tc_detect_color(const uint8_t *rgb, int n_pixels) {
  if (rgb == 0 || n_pixels <= 0) return CLASS_NONE;

  long cnt[4] = {0, 0, 0, 0};  // yellow, red, green, blue
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

    if (h < 20.0f || h >= 345.0f)
      cnt[1]++;  // red (wraps around 0/360)
    else if (h >= 40.0f && h < 75.0f)
      cnt[0]++;  // yellow
    else if (h >= 80.0f && h < 165.0f)
      cnt[2]++;  // green
    else if (h >= 190.0f && h < 255.0f)
      cnt[3]++;  // blue
  }

  int best = -1;
  long bestN = 0;
  for (int c = 0; c < 4; c++)
    if (cnt[c] > bestN) {
      bestN = cnt[c];
      best = c;
    }
  if (best < 0 || (float)bestN < COVER_MIN * (float)n_pixels) return CLASS_NONE;

  static const int cls[4] = {CLASS_YELLOW, CLASS_RED, CLASS_GREEN, CLASS_BLUE};
  return cls[best];
}
