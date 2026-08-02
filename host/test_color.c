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

// Paint columns [x0, x1) of every row yellow (a vertical block at a known place).
static void paint(uint8_t *buf, int w, int h, int x0, int x1) {
  for (int y = 0; y < h; y++)
    for (int x = x0; x < x1; x++) {
      int i = y * w + x;
      buf[i * 3 + 0] = 255;
      buf[i * 3 + 1] = 220;
      buf[i * 3 + 2] = 0;
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

static void checkNear(const char *name, int got, int want, int tol) {
  if (got < want - tol || got > want + tol) {
    printf("FAIL %-12s got %d want %d±%d\n", name, got, want, tol);
    fails++;
  } else {
    printf("ok   %-12s -> %d (~%d)\n", name, got, want);
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
  // Orange and pink are their own classes: they used to read "nothing", which a
  // program cannot tell apart from a covered lens.
  fill(buf, n, 255, 120, 0);   check("orange", tc_detect_color(buf, n), 8);
  fill(buf, n, 255, 60, 160);  check("pink",   tc_detect_color(buf, n), 9);
  // Amber/gold must NOT be red: widening red to swallow orange was measured on
  // real photos and rejected exactly because it relabels these.
  fill(buf, n, 212, 160, 23);  check("gold",   tc_detect_color(buf, n), 4);

  // Half the frame yellow, half grey -> yellow (covers 50%, above COVER_MIN).
  fill(buf, n, 128, 128, 128);
  for (int i = 0; i < n / 2; i++) {
    buf[i * 3] = 255;
    buf[i * 3 + 1] = 220;
    buf[i * 3 + 2] = 0;
  }
  check("half-yellow", tc_detect_color(buf, n), 4);

  // A distant ball fills only a few percent of the frame. It IS seen (the class
  // fires) — how close is close enough is the program's call, via SEESIZE().
  fill(buf, n, 128, 128, 128);
  for (int i = 0; i < n / 20; i++) {
    buf[i * 3] = 255;
    buf[i * 3 + 1] = 220;
    buf[i * 3 + 2] = 0;
  }
  check("far-yellow", tc_detect_color(buf, n), 4);

  // A handful of stray pixels is noise, not an object.
  fill(buf, n, 128, 128, 128);
  for (int i = 0; i < 20; i++) {
    buf[i * 3] = 255;
    buf[i * 3 + 1] = 220;
    buf[i * 3 + 2] = 0;
  }
  check("speck", tc_detect_color(buf, n), 0);

  // ── AN OBJECT BEATS A BACKGROUND (the bench failure this exists for) ──
  //
  // On a real desk the warm wood and the lamp light produced MORE yellow pixels
  // than a marker held up to the lens, so counting pixels elected the room every
  // time and no threshold could fix it. What separates the two is SHAPE: an
  // object is a compact region, a background is colour scattered everywhere.
  {
    const int bw = 48, bh = 48;
    fill(buf, n, 128, 128, 128);
    // Background: warm speckle over the whole frame — a LOT of yellow pixels,
    // ~14% of them, but never touching, so no region is ever an object.
    for (int y = 0; y < bh; y++)
      for (int x = 0; x < bw; x++)
        if (((x + y) % 3) == 0 && (x % 2) == 0) {
          const int i = y * bw + x;
          buf[i * 3] = 255;
          buf[i * 3 + 1] = 220;
          buf[i * 3 + 2] = 0;
        }
    check("speckle-is-not-an-object", tc_detect_color_blob(buf, bw, bh).cls, 0);

    // Now hold a SMALL blue square in the middle: fewer pixels than the warm
    // speckle, and it must still win, because it is the only thing shaped like
    // an object.
    for (int y = 19; y < 29; y++)
      for (int x = 19; x < 29; x++) {
        const int i = y * bw + x;
        buf[i * 3] = 20;
        buf[i * 3 + 1] = 60;
        buf[i * 3 + 2] = 220;
      }
    tc_color_blob held = tc_detect_color_blob(buf, bw, bh);
    check("object-beats-background", held.cls, 7);
    checkNear("held-x", held.x, 50, 10);
  }

  // ── WHERE and HOW BIG (SEEX() / SEESIZE()) ──
  const int w = 48, h = 48;

  // A block down the LEFT quarter -> centroid near the left edge, ~25% coverage.
  fill(buf, n, 128, 128, 128);
  paint(buf, w, h, 0, w / 4);
  tc_color_blob left = tc_detect_color_blob(buf, w, h);
  check("left-cls", left.cls, 4);
  checkNear("left-x", left.x, 12, 8);
  checkNear("left-size", left.size, 25, 3);

  // The same block on the RIGHT -> the centroid moves to the other side while
  // the size is unchanged (position and size are independent readings).
  fill(buf, n, 128, 128, 128);
  paint(buf, w, h, w - w / 4, w);
  tc_color_blob right = tc_detect_color_blob(buf, w, h);
  checkNear("right-x", right.x, 88, 8);
  checkNear("right-size", right.size, 25, 3);

  // Getting closer = filling more of the frame; the centre stays centred.
  fill(buf, n, 128, 128, 128);
  paint(buf, w, h, w / 4, w - w / 4);
  tc_color_blob near = tc_detect_color_blob(buf, w, h);
  checkNear("near-x", near.x, 50, 6);
  checkNear("near-size", near.size, 50, 3);

  // Nothing seen -> the whole reading is zero, so SEESIZE() > 0 is the honest
  // gate before trusting SEEX() (0 is also a legitimate far-left position).
  fill(buf, n, 128, 128, 128);
  tc_color_blob none = tc_detect_color_blob(buf, w, h);
  check("none-cls", none.cls, 0);
  check("none-x", none.x, 0);
  check("none-size", none.size, 0);

  free(buf);
  if (fails) {
    printf("FAILED %d\n", fails);
    return 1;
  }
  printf("OK: color_detect matches the SEE()/SEEX()/SEESIZE() contract\n");
  return 0;
}
