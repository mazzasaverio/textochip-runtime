// The near-term VISION flagship, proven on the firmware VM with NO camera: an
// RGB frame -> color_detect (the colour class) -> injected with setVisionClass()
// (what the vision service will do after color_detect on a real Arducam frame)
// -> the program stops the robot when it SEE()s yellow, and roams otherwise.
// This is the colour twin of test_ai_move (voice -> motors). Build & run:
//   make test-color-move
#include <cstdio>
#include <cstdlib>

#include "../src/ai/color_detect.h"
#include "../src/isa.h"
#include "../src/vm.h"
#include "hal_host.h"

// Lowered like the product compiles "roam, but stop when you see yellow":
//   0 INFER vision   1 PUSH 4   2 EQ   3 JZ 6   4 MOVE 0 0   5 JMP 7
//   6 MOVE 150 150   7 JMP 0
// SEE()="yellow" is class 4 (VISION_LABELS: objects 1..3, then colours 4..7).
static const char *PROGRAM[] = {
    "INFER vision", "PUSH 4",      "EQ",     "JZ 6", "MOVE 0 0",
    "JMP 7",        "MOVE 150 150", "JMP 0",
};

static void drive_with_frame(const uint8_t *rgb, int n, int *left, int *right) {
  int cls = tc_detect_color(rgb, n);
  VM vm;
  vm.reset();
  for (const char *line : PROGRAM) {
    Instruction in;
    if (parseInstructionLine(line, in)) vm.addInstruction(in);
  }
  vm.setVisionClass(cls);  // the vision service injects color_detect's result
  vm.start();
  for (int i = 0; i < 12; i++) vm.tick();  // let the poll loop run several passes
  host_get_move(left, right);
}

// The HUNT: the same camera frame answers WHAT (SEE()), WHERE (SEEX()) and HOW
// BIG (SEESIZE()), so the robot can spin looking for the ball, steer at it, and
// stop once it fills enough of the view. Lowered like the product compiles
// examples.ts TROVAPALLA (threshold 45):
//   0 INFER visionsize  1 PUSH 0   2 EQ   3 JZ 6   4 MOVE 120 -120  5 JMP 20
//   6 INFER visionsize  7 PUSH 45  8 LT   9 NOT   10 JZ 13  11 MOVE 0 0  12 JMP 20
//  13 INFER visionx    14 PUSH 60 15 GT  16 JZ 19  17 MOVE 180 70   18 JMP 20
//  19 MOVE 160 160     20 JMP 0
// (SEESIZE() >= 45 is how the compiler emits it: LT then NOT.)
static const char *HUNT[] = {
    "INFER visionsize", "PUSH 0",       "EQ",     "JZ 6",  "MOVE 120 -120",
    "JMP 20",           "INFER visionsize", "PUSH 45", "LT", "NOT",
    "JZ 13",            "MOVE 0 0",     "JMP 20", "INFER visionx", "PUSH 60",
    "GT",               "JZ 19",        "MOVE 180 70", "JMP 20", "MOVE 160 160",
    "JMP 0",
};

static void hunt_with_frame(const uint8_t *rgb, int w, int h, int *left, int *right) {
  tc_color_blob blob = tc_detect_color_blob(rgb, w, h);
  VM vm;
  vm.reset();
  for (const char *line : HUNT) {
    Instruction in;
    if (parseInstructionLine(line, in)) vm.addInstruction(in);
  }
  // What the vision service does after one real frame: all three from one blob.
  vm.setVisionClass(blob.cls);
  vm.setVisionX(blob.x);
  vm.setVisionSize(blob.size);
  vm.start();
  for (int i = 0; i < 12; i++) vm.tick();
  host_get_move(left, right);
}

// Paint columns [x0, x1) yellow — a ball at a known place in the frame.
static void paint(uint8_t *b, int w, int h, int x0, int x1) {
  for (int y = 0; y < h; y++)
    for (int x = x0; x < x1; x++) {
      int i = y * w + x;
      b[i * 3] = 255;
      b[i * 3 + 1] = 220;
      b[i * 3 + 2] = 0;
    }
}

static void fill(uint8_t *b, int n, int r, int g, int bl) {
  for (int i = 0; i < n; i++) {
    b[i * 3] = (uint8_t)r;
    b[i * 3 + 1] = (uint8_t)g;
    b[i * 3 + 2] = (uint8_t)bl;
  }
}

int main(void) {
  const int n = 48 * 48;
  uint8_t *buf = (uint8_t *)malloc((size_t)n * 3);
  int fail = 0, l = -1, r = -1;

  fill(buf, n, 255, 220, 0);  // a yellow object fills the view -> STOP
  drive_with_frame(buf, n, &l, &r);
  printf("  yellow frame -> MOVE %d %d (want 0 0)\n", l, r);
  if (l != 0 || r != 0) fail = 1;

  fill(buf, n, 128, 128, 128);  // nothing coloured -> keep roaming
  drive_with_frame(buf, n, &l, &r);
  printf("  grey frame   -> MOVE %d %d (want 150 150)\n", l, r);
  if (l != 150 || r != 150) fail = 1;

  // ── the hunt: WHERE + HOW BIG steer the robot ──
  const int w = 48, h = 48;

  fill(buf, n, 128, 128, 128);  // nothing in view -> spin and search
  hunt_with_frame(buf, w, h, &l, &r);
  printf("  no ball      -> MOVE %d %d (want 120 -120)\n", l, r);
  if (l != 120 || r != -120) fail = 1;

  fill(buf, n, 128, 128, 128);  // a small ball off to the RIGHT -> turn right
  paint(buf, w, h, w - w / 5, w);
  hunt_with_frame(buf, w, h, &l, &r);
  printf("  ball right   -> MOVE %d %d (want 180 70)\n", l, r);
  if (l != 180 || r != 70) fail = 1;

  fill(buf, n, 128, 128, 128);  // centred and far -> drive straight at it
  paint(buf, w, h, w / 2 - w / 10, w / 2 + w / 10);
  hunt_with_frame(buf, w, h, &l, &r);
  printf("  ball ahead   -> MOVE %d %d (want 160 160)\n", l, r);
  if (l != 160 || r != 160) fail = 1;

  fill(buf, n, 255, 220, 0);  // it fills the view -> close enough, stop
  hunt_with_frame(buf, w, h, &l, &r);
  printf("  ball close   -> MOVE %d %d (want 0 0)\n", l, r);
  if (l != 0 || r != 0) fail = 1;

  free(buf);
  if (!fail)
    printf(
        "OK: RGB frame -> color_detect -> SEE()/SEEX()/SEESIZE() -> the robot "
        "stops at yellow, and hunts the ball (spin, steer, approach, stop)\n");
  else
    printf("FAIL: wrong motor output\n");
  return fail;
}
