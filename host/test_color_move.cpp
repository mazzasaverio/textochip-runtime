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

  free(buf);
  if (!fail)
    printf(
        "OK: RGB frame -> color_detect -> SEE() -> the robot stops at yellow, "
        "roams otherwise\n");
  else
    printf("FAIL: wrong motor output\n");
  return fail;
}
