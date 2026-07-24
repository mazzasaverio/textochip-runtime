// Colour-blob detection for SEE() — the near-term camera path (a request like
// "stop at a yellow object" needs COLOUR, which a trained classifier does not
// give cheaply). Given an RGB frame it returns the dominant saturated colour as
// a vision class index, so `SEE()="yellow"` fires when a yellow thing fills
// enough of the view. Pure, portable C (no float lib needed): the SAME code runs
// on the host (this test) and on the board fed by the Arducam capture. Mirrors
// how features.c is the portable, host-tested core of the voice path.
#ifndef TC_COLOR_DETECT_H
#define TC_COLOR_DETECT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// What the frame shows: WHAT, WHERE and HOW BIG — the three reads the product
// exposes as SEE() / SEEX() / SEESIZE() (lib/missions/vision.ts). Knowing only
// the class is not actionable: "find the ball" needs a direction to steer in and
// a way to tell near from far.
typedef struct {
  int cls;   // vision class index (0 = nothing recognised)
  int x;     // horizontal centroid, 0 = far left … 100 = far right (0 if cls == 0)
  int size;  // share of the frame covered, 0..100 (0 if cls == 0)
} tc_color_blob;

// Detect the dominant saturated colour in an RGB frame (3 bytes/pixel: R,G,B),
// laid out row-major, `width` pixels per row. The class MUST match the product's
// VISION_LABELS (lib/missions/vision.ts): objects person/ball/hand = 1/2/3, then
//   4 = yellow, 5 = red, 6 = green, 7 = blue, 0 = none
// Reports a blob as soon as it is above the noise floor (a few percent of the
// frame) — NOT only when it fills the view. How close is close enough is the
// PROGRAM's call, through `size` (SEESIZE()), not a threshold baked in here.
tc_color_blob tc_detect_color_blob(const uint8_t *rgb, int width, int height);

// Class only (the frame's geometry does not change WHAT is seen), for callers
// that don't care where it is.
int tc_detect_color(const uint8_t *rgb, int n_pixels);

#ifdef __cplusplus
}
#endif

#endif  // TC_COLOR_DETECT_H
