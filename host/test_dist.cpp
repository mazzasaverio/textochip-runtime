// DIST (ultrasonic distance) end-to-end on the firmware VM, no sensor: the host
// stub feeds a distance in cm, the program reads DISTANCE() (-> DIST) and stops
// the robot when something is closer than a threshold, otherwise roams. This is
// the distance twin of test_color_move / test_ai_move. Build & run:
//   make test-dist
#include <cstdio>

#include "../src/isa.h"
#include "../src/vm.h"
#include "hal_host.h"

// Lowered like the product compiles "roam, but stop closer than 10 cm":
//   0 DIST   1 PUSH 10   2 LT   3 JZ 6   4 MOVE 0 0   5 JMP 7
//   6 MOVE 150 150   7 JMP 0
// LT pops b,a and pushes (a<b): a=distance, b=10 -> (distance < 10). When true
// (close) JZ does NOT jump -> MOVE 0 0 (stop); when false -> MOVE 150 150 (roam).
static const char *PROGRAM[] = {
    "DIST",  "PUSH 10",      "LT",     "JZ 6", "MOVE 0 0",
    "JMP 7", "MOVE 150 150", "JMP 0",
};

static void drive_at(int cm, int *left, int *right) {
  host_set_distance(cm);
  VM vm;
  vm.reset();
  for (const char *line : PROGRAM) {
    Instruction in;
    if (parseInstructionLine(line, in)) vm.addInstruction(in);
  }
  vm.start();
  for (int i = 0; i < 12; i++) vm.tick();
  host_get_move(left, right);
}

int main(void) {
  int fail = 0, l = -1, r = -1;

  drive_at(5, &l, &r);  // something 5 cm ahead -> STOP
  printf("  5 cm  -> MOVE %d %d (want 0 0)\n", l, r);
  if (l != 0 || r != 0) fail = 1;

  drive_at(100, &l, &r);  // clear ahead -> keep roaming
  printf("  100 cm -> MOVE %d %d (want 150 150)\n", l, r);
  if (l != 150 || r != 150) fail = 1;

  if (!fail)
    printf(
        "OK: DIST -> DISTANCE() -> the robot stops within 10 cm, roams "
        "otherwise\n");
  else
    printf("FAIL: wrong motor output\n");
  return fail;
}
