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
// The MIDDLE of the frame WEIGHS MORE in the vote — it does not crop it.
//
// Bench evidence: with nothing held up, the desk and the warm light won at ~10%
// of the frame, and a blue object presented to the camera never entered the
// contest at all — the background merely shifted from yellow to orange (the
// camera's auto white balance warming everything else to compensate for the
// blue). A small object at arm's length cannot outvote a table, and it should
// not have to: a robot cares about what is in FRONT of it.
//
// Cropping to the centre was the obvious fix and it is WRONG: a robot spinning
// to find a target must see it as it ENTERS the frame, at the edge, and SEEX()
// must keep spanning the whole view or "it is on my left" stops meaning
// anything. So the weighting applies ONLY to which colour wins. Coverage and
// centroid stay measured over the whole frame, exactly as before, so every
// threshold a maker has already tuned keeps its meaning.
#define ROI 0.66f        // central fraction; a region overlapping it is preferred
#define FILL_MIN 0.30f   // a region must fill this much of its bounding box
#define MARGIN 1.3f      // the winner must beat the runner-up by this much
#define COVER_MIN 0.02f  // 2% of the pixels — noise floor, not a "close enough" test

// The largest CONNECTED REGION of one colour, not the commonest colour.
//
// Counting pixels over the whole frame answers "is there a lot of yellow in this
// picture", which is not the question. A wooden desk spread across the view
// produces more warm pixels than a marker held up to the lens, so the desk won
// every vote and no threshold could change that — an object is a COMPACT REGION
// with a boundary and a place, and a background is colour scattered everywhere.
// Bench evidence that forced this: holding a blue object up returned "orange",
// because the background had merely warmed.
//
// So: classify each pixel, flood-fill the regions, and report the biggest one
// that is actually shaped like an object. SEEX()/SEESIZE() then describe THAT
// region, which is what a program following something has always meant by them.

#define MAX_PIXELS 16384  // 128x128 — bigger frames are rejected, not truncated

static unsigned char g_mask[MAX_PIXELS];  // 0 = no colour, else 1 + class slot
static unsigned char g_seen[MAX_PIXELS];
// The flood-fill stack holds PIXEL INDICES, so its values are bounded by
// MAX_PIXELS (16383 max) and 16 bits are two bytes more than enough. As `int`
// this single array was 64 KB — on a DK image already at 92% of RAM, the
// largest object in the build and 32 KB of pure padding. Widening back to int
// on pop is free.
static uint16_t g_stack[MAX_PIXELS];

tc_color_blob tc_detect_color_blob(const uint8_t *rgb, int width, int height) {
  tc_color_blob out;
  out.cls = CLASS_NONE;
  out.x = 0;
  out.size = 0;
  if (rgb == 0 || width <= 0 || height <= 0) return out;
  const int n_pixels = width * height;
  if (n_pixels > MAX_PIXELS) return out;

  // The centre box: a region overlapping it is preferred, because a robot cares
  // about what is in FRONT of it. This never CROPS — a target entering at the
  // edge must still be seen, or a robot spinning to find one never would.
  const int cx0 = (int)(width * (1.0f - ROI) * 0.5f);
  const int cx1 = width - cx0;
  const int cy0 = (int)(height * (1.0f - ROI) * 0.5f);
  const int cy1 = height - cy0;

  for (int i = 0; i < n_pixels; i++) {
    g_mask[i] = 0;
    g_seen[i] = 0;
    float r = rgb[i * 3 + 0] / 255.0f;
    float g = rgb[i * 3 + 1] / 255.0f;
    float b = rgb[i * 3 + 2] / 255.0f;
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float d = mx - mn;
    if (mx < VAL_MIN) continue;
    if (d < SAT_MIN * mx) continue;
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
      c = 1;  // red
    else if (h >= 40.0f && h < 80.0f)
      c = 0;  // yellow
    else if (h >= 80.0f && h < 175.0f)
      c = 2;  // green
    else if (h >= 175.0f && h < 255.0f)
      c = 3;  // blue
    else if (h >= 20.0f && h < 40.0f)
      c = 4;  // orange
    else if (h >= 285.0f && h < 345.0f)
      c = 5;  // pink
    else
      continue;  // violet — deliberately uncovered
    g_mask[i] = (unsigned char)(c + 1);
  }

  int bestC = -1;
  long bestN = 0, bestSumX = 0;
  long bestScore = 0;

  for (int start = 0; start < n_pixels; start++) {
    if (g_mask[start] == 0 || g_seen[start]) continue;
    const unsigned char m = g_mask[start];
    int sp = 0;
    g_stack[sp++] = start;
    g_seen[start] = 1;
    long n = 0, sumx = 0;
    int minx = width, maxx = -1, miny = height, maxy = -1;
    long inCentre = 0;
    while (sp > 0) {
      const int p = g_stack[--sp];
      const int px = p % width, py = p / width;
      n++;
      sumx += px;
      if (px < minx) minx = px;
      if (px > maxx) maxx = px;
      if (py < miny) miny = py;
      if (py > maxy) maxy = py;
      if (px >= cx0 && px < cx1 && py >= cy0 && py < cy1) inCentre++;
      // 4-connectivity: diagonals would bridge two objects that merely touch
      // at a corner, and on a noisy sensor they bridge noise into everything.
      if (px > 0 && g_mask[p - 1] == m && !g_seen[p - 1]) {
        g_seen[p - 1] = 1;
        g_stack[sp++] = p - 1;
      }
      if (px + 1 < width && g_mask[p + 1] == m && !g_seen[p + 1]) {
        g_seen[p + 1] = 1;
        g_stack[sp++] = p + 1;
      }
      if (py > 0 && g_mask[p - width] == m && !g_seen[p - width]) {
        g_seen[p - width] = 1;
        g_stack[sp++] = p - width;
      }
      if (py + 1 < height && g_mask[p + width] == m && !g_seen[p + width]) {
        g_seen[p + width] = 1;
        g_stack[sp++] = p + width;
      }
    }

    if ((float)n < COVER_MIN * (float)n_pixels) continue;  // noise floor

    // Is it SHAPED like an object? A real one fills a good part of its bounding
    // box; a background scattered across the view has a huge box and rattles
    // around inside it. This is the test that finally separates a marker from a
    // desk, and no brightness threshold ever could.
    const long boxArea = (long)(maxx - minx + 1) * (long)(maxy - miny + 1);
    if (boxArea > 0 && (float)n < FILL_MIN * (float)boxArea) continue;

    // Among the real objects, prefer the biggest — with the part inside the
    // centre box counted twice, so what the robot faces wins a close call.
    const long score = n + inCentre;
    if (score > bestScore) {
      bestScore = score;
      bestN = n;
      bestSumX = sumx;
      bestC = m - 1;
    }
  }

  if (bestC < 0) return out;

  static const int cls[6] = {CLASS_YELLOW, CLASS_RED, CLASS_GREEN, CLASS_BLUE,
                             CLASS_ORANGE, CLASS_PINK};
  out.cls = cls[bestC];
  // Centroid column -> 0..100, across the FULL width: "on my left" has to keep
  // meaning the left of everything the camera can see.
  out.x = width > 1 ? (int)((bestSumX / bestN) * 100 / (width - 1)) : 50;
  // Coverage -> 0..100 of the whole frame, so thresholds already tuned by a
  // maker keep their meaning.
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

void tc_color_hist(const unsigned char* rgb, int width, int height, long* hist12) {
  if (!hist12) return;
  for (int i = 0; i < 12; i++) hist12[i] = 0;
  if (!rgb || width <= 0 || height <= 0) return;
  const int n_pixels = width * height;
  for (int i = 0; i < n_pixels; i++) {
    float r = rgb[i * 3 + 0] / 255.0f;
    float g = rgb[i * 3 + 1] / 255.0f;
    float b = rgb[i * 3 + 2] / 255.0f;
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float d = mx - mn;
    if (mx < VAL_MIN) continue;
    if (d < SAT_MIN * mx) continue;
    float h;
    if (mx == r)
      h = 60.0f * ((g - b) / d);
    else if (mx == g)
      h = 60.0f * ((b - r) / d + 2.0f);
    else
      h = 60.0f * ((r - g) / d + 4.0f);
    if (h < 0.0f) h += 360.0f;
    int bucket = (int)(h / 30.0f);
    if (bucket < 0) bucket = 0;
    if (bucket > 11) bucket = 11;
    hist12[bucket]++;
  }
}

