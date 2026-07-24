// color_probe — what the BOARD would see in a real photograph.
//
// The colour detector (src/ai/color_detect.c) is unit-tested on synthetic frames:
// flat patches of known RGB. Real light is not flat. A yellow ball under a warm
// bulb, a green wall in shadow, a white page under tungsten — those are where a
// hue/saturation threshold either earns its keep or fires at nothing. This tool
// runs the REAL detector (no reimplementation, so it cannot drift) over a real
// photo, through the same pipeline the camera will feed it:
//
//   photo -> centre-crop to square -> box-downscale to 96x96 -> RGB565 -> detector
//
// The crop + scale mirror the frame the vision service assembles; the RGB565 step
// mirrors what the Arducam Mega actually delivers (it costs the low bits of each
// channel, which is exactly the kind of thing that nudges a hue past a threshold).
// Pass --no565 to see the difference.
//
// Input is a binary PPM (P6) so this stays dependency-free C; tools/img2ppm.py
// converts any jpg/png/webp, and `make color-probe IMG=photo.jpg` does both.
//
// It prints WHAT the detector saw (SEE()), WHERE (SEEX()) and HOW BIG (SEESIZE()),
// plus a coarse map of the frame so you can check the position against the photo
// with your own eyes. Each map cell is classified by the SAME detector.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/ai/color_detect.h"

#define FRAME 96  // the vision service's frame is 96x96 (see vision_service.cpp)

static const char *class_name(int cls) {
  switch (cls) {
    case 0: return "nothing";
    case 1: return "person";
    case 2: return "ball";
    case 3: return "hand";
    case 4: return "yellow";
    case 5: return "red";
    case 6: return "green";
    case 7: return "blue";
    case 8: return "orange";
    case 9: return "pink";
    default: return "?";
  }
}

static char class_char(int cls) {
  switch (cls) {
    case 4: return 'Y';
    case 5: return 'R';
    case 6: return 'G';
    case 7: return 'B';
    case 8: return 'O';
    case 9: return 'P';
    default: return '.';
  }
}

// ── PPM (P6) reading ──────────────────────────────────────────────────────────
// Skips whitespace and '#' comments between the header fields, as the format
// allows them anywhere.
static int read_header_int(FILE *f, int *out) {
  int c, v = 0, got = 0;
  for (;;) {
    c = fgetc(f);
    if (c == EOF) return 0;
    if (c == '#') {
      while (c != '\n' && c != EOF) c = fgetc(f);
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (got) break;
      continue;
    }
    if (c < '0' || c > '9') return 0;
    v = v * 10 + (c - '0');
    got = 1;
  }
  *out = v;
  return got;
}

static uint8_t *read_ppm(const char *path, int *w, int *h) {
  FILE *f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "cannot open %s\n", path);
    return NULL;
  }
  char magic[3] = {0};
  if (fread(magic, 1, 2, f) != 2 || strcmp(magic, "P6") != 0) {
    fprintf(stderr, "%s: not a binary PPM (P6) — convert with tools/img2ppm.py\n", path);
    fclose(f);
    return NULL;
  }
  int maxval = 0;
  if (!read_header_int(f, w) || !read_header_int(f, h) || !read_header_int(f, &maxval) ||
      *w <= 0 || *h <= 0 || maxval != 255) {
    fprintf(stderr, "%s: unsupported PPM header (need 8-bit)\n", path);
    fclose(f);
    return NULL;
  }
  size_t n = (size_t)*w * (size_t)*h * 3;
  uint8_t *buf = (uint8_t *)malloc(n);
  if (!buf || fread(buf, 1, n, f) != n) {
    fprintf(stderr, "%s: truncated pixel data\n", path);
    free(buf);
    fclose(f);
    return NULL;
  }
  fclose(f);
  return buf;
}

// ── the camera pipeline ───────────────────────────────────────────────────────
// Centre-crop to a square (a camera does not stretch the world) then average each
// source block into one output pixel — the honest way to shrink, and close to what
// a sensor's binning does. Averaging matters: nearest-neighbour would keep single
// bright pixels that box-averaging correctly dilutes.
static void crop_scale(const uint8_t *src, int sw, int sh, uint8_t *dst) {
  int side = sw < sh ? sw : sh;
  int x0 = (sw - side) / 2, y0 = (sh - side) / 2;
  for (int y = 0; y < FRAME; y++) {
    int sy0 = y0 + y * side / FRAME, sy1 = y0 + (y + 1) * side / FRAME;
    if (sy1 <= sy0) sy1 = sy0 + 1;
    for (int x = 0; x < FRAME; x++) {
      int sx0 = x0 + x * side / FRAME, sx1 = x0 + (x + 1) * side / FRAME;
      if (sx1 <= sx0) sx1 = sx0 + 1;
      long r = 0, g = 0, b = 0, n = 0;
      for (int sy = sy0; sy < sy1; sy++)
        for (int sx = sx0; sx < sx1; sx++) {
          const uint8_t *p = src + ((size_t)sy * sw + sx) * 3;
          r += p[0];
          g += p[1];
          b += p[2];
          n++;
        }
      uint8_t *q = dst + ((size_t)y * FRAME + x) * 3;
      q[0] = (uint8_t)(r / n);
      q[1] = (uint8_t)(g / n);
      q[2] = (uint8_t)(b / n);
    }
  }
}

// What survives the camera's RGB565 transport: 5 bits R, 6 G, 5 B, expanded back
// to 8 bits the way the driver does (replicate the high bits).
static void quantize565(uint8_t *f, int n_pixels) {
  for (int i = 0; i < n_pixels; i++) {
    uint8_t *p = f + i * 3;
    uint8_t r5 = p[0] >> 3, g6 = p[1] >> 2, b5 = p[2] >> 3;
    p[0] = (uint8_t)((r5 << 3) | (r5 >> 2));
    p[1] = (uint8_t)((g6 << 2) | (g6 >> 4));
    p[2] = (uint8_t)((b5 << 3) | (b5 >> 2));
  }
}

// A coarse map of the frame, each cell classified by the SAME detector, so you
// can check WHERE against the photo. A cell is small, so its own noise floor is
// ~1 pixel: the map is more trigger-happy than the whole-frame verdict by design
// (it shows where colour lives, not what the program would act on).
static void print_map(const uint8_t *frame, int cells) {
  int step = FRAME / cells;
  uint8_t *cell = (uint8_t *)malloc((size_t)step * step * 3);
  printf("  frame map (%dx%d cells, . = nothing):\n", cells, cells);
  for (int cy = 0; cy < cells; cy++) {
    printf("    ");
    for (int cx = 0; cx < cells; cx++) {
      for (int y = 0; y < step; y++)
        memcpy(cell + (size_t)y * step * 3,
               frame + (((size_t)(cy * step + y) * FRAME) + cx * step) * 3,
               (size_t)step * 3);
      tc_color_blob b = tc_detect_color_blob(cell, step, step);
      putchar(class_char(b.cls));
      putchar(' ');
    }
    putchar('\n');
  }
  free(cell);
}

int main(int argc, char **argv) {
  const char *path = NULL;
  int use565 = 1, cells = 12;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--no565") == 0)
      use565 = 0;
    else if (strcmp(argv[i], "--cells") == 0 && i + 1 < argc)
      cells = atoi(argv[++i]);
    else
      path = argv[i];
  }
  if (!path) {
    fprintf(stderr,
            "usage: color_probe [--no565] [--cells N] <photo.ppm>\n"
            "  (convert a jpg/png with tools/img2ppm.py, or use "
            "`make color-probe IMG=photo.jpg`)\n");
    return 2;
  }
  if (cells < 1 || cells > FRAME || FRAME % cells) cells = 12;

  int w = 0, h = 0;
  uint8_t *img = read_ppm(path, &w, &h);
  if (!img) return 1;

  uint8_t frame[FRAME * FRAME * 3];
  crop_scale(img, w, h, frame);
  free(img);
  if (use565) quantize565(frame, FRAME * FRAME);

  tc_color_blob b = tc_detect_color_blob(frame, FRAME, FRAME);
  printf("%s\n", path);
  printf("  source %dx%d -> centre-crop -> %dx%d%s\n", w, h, FRAME, FRAME,
         use565 ? " -> RGB565" : " (no RGB565)");
  printf("  SEE()      = %d (%s)\n", b.cls, class_name(b.cls));
  printf("  SEEX()     = %-3d %s\n", b.x,
         b.cls == 0 ? "(nothing seen)" : b.x < 40 ? "(left)" : b.x > 60 ? "(right)" : "(centred)");
  printf("  SEESIZE()  = %-3d %s\n", b.size,
         b.cls == 0 ? "(nothing seen)" : b.size >= 45 ? "(close)" : "(far)");
  print_map(frame, cells);
  return 0;
}
