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

// Detect the dominant saturated colour in an RGB frame (3 bytes/pixel: R,G,B).
// Returns the vision class index, which MUST match the product's VISION_LABELS
// (lib/missions/vision.ts): objects person/ball/hand = 1/2/3, then colours
//   4 = yellow, 5 = red, 6 = green, 7 = blue, 0 = none
// "none" when no known colour covers enough of the frame (grey/dark/mixed).
int tc_detect_color(const uint8_t *rgb, int n_pixels);

#ifdef __cplusplus
}
#endif

#endif  // TC_COLOR_DETECT_H
