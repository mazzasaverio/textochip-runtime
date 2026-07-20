// Host test for the colour-blob detector (the near-term SEE() camera path).
// Synthetic RGB frames -> the SEE() colour class, pinning the contract with the
// product (lib/missions/vision.ts: yellow=4, red=5, green=6, blue=7, none=0).
#include <stdio.h>
#include <stdlib.h>

#include "../src/ai/color_detect.h"

static void fill(uint8_t *buf, int n, int r, int g, int b) {
  for (int i = 0; i < n; i++) {
    buf[i * 3 + 0] = (uint8_t)r;
    buf[i * 3 + 1] = (uint8_t)g;
    buf[i * 3 + 2] = (uint8_t)b;
  }
}

static int fails = 0;
static void check(const char *name, int got, int want) {
  if (got != want) {
    printf("FAIL %-12s got %d want %d\n", name, got, want);
    fails++;
  } else {
    printf("ok   %-12s -> %d\n", name, got);
  }
}

int main(void) {
  const int n = 48 * 48;
  uint8_t *buf = (uint8_t *)malloc((size_t)n * 3);

  fill(buf, n, 255, 220, 0);   check("yellow", tc_detect_color(buf, n), 4);
  fill(buf, n, 230, 10, 10);   check("red",    tc_detect_color(buf, n), 5);
  fill(buf, n, 20, 200, 40);   check("green",  tc_detect_color(buf, n), 6);
  fill(buf, n, 20, 60, 220);   check("blue",   tc_detect_color(buf, n), 7);
  fill(buf, n, 128, 128, 128); check("grey",   tc_detect_color(buf, n), 0);
  fill(buf, n, 0, 0, 0);       check("black",  tc_detect_color(buf, n), 0);
  fill(buf, n, 255, 255, 255); check("white",  tc_detect_color(buf, n), 0);

  // Half the frame yellow, half grey -> yellow (covers 50%, above COVER_MIN).
  fill(buf, n, 128, 128, 128);
  for (int i = 0; i < n / 2; i++) {
    buf[i * 3] = 255;
    buf[i * 3 + 1] = 220;
    buf[i * 3 + 2] = 0;
  }
  check("half-yellow", tc_detect_color(buf, n), 4);

  // A tiny yellow speck (~5%) is below COVER_MIN -> nothing (don't false-fire).
  fill(buf, n, 128, 128, 128);
  for (int i = 0; i < n / 20; i++) {
    buf[i * 3] = 255;
    buf[i * 3 + 1] = 220;
    buf[i * 3 + 2] = 0;
  }
  check("tiny-yellow", tc_detect_color(buf, n), 0);

  free(buf);
  if (fails) {
    printf("FAILED %d\n", fails);
    return 1;
  }
  printf("OK: color_detect matches the SEE() colour contract\n");
  return 0;
}
