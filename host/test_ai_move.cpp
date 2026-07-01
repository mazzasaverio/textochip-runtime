// Firmware VM de-risk for the configurator's VOICE hero: the EXACT bytecode the
// product compiles for "drive the robot by voice" runs on the real firmware VM,
// and the detected keyword drives the L298N (MOVE). Proves the same program that
// works in the browser simulator drives real motors on the board — the class is
// injected with setAiClass() (what the background inference service does), so no
// TFLM and no mic are needed here. Build & run:  make test-ai-move
#include <cstdio>

#include "../src/isa.h"
#include "../src/vm.h"
#include "hal_host.h"

// The product lowers the configurator's voice program to exactly this (verified
// against lib/compiler): each VOICE() word -> INFER ; PUSH <idx> ; EQ ; JZ ; MOVE.
// VOICE_LABELS: go=1, left=2, right=3, stop=4. It loops (JMP 0), like a robot that
// keeps obeying the last word (MOVE is sticky) until it hears the next one.
static const char* PROGRAM[] = {
    "INFER voice", "PUSH 1", "EQ", "JZ 5",  "MOVE 160 160",  // go    -> forward
    "INFER voice", "PUSH 4", "EQ", "JZ 10", "MOVE 0 0",      // stop  -> halt
    "INFER voice", "PUSH 2", "EQ", "JZ 15", "MOVE 40 160",   // left  -> curve left
    "INFER voice", "PUSH 3", "EQ", "JZ 20", "MOVE 160 40",   // right -> curve right
    "JMP 0",
};

static void drive_with_class(long ai_class, int* left, int* right) {
  VM vm;
  vm.reset();
  for (const char* line : PROGRAM) {
    Instruction in;
    if (parseInstructionLine(line, in)) vm.addInstruction(in);
  }
  vm.setAiClass(ai_class);  // the inference service injects the heard keyword
  vm.start();
  for (int i = 0; i < 12; i++) vm.tick();  // let the poll loop run several passes
  host_get_move(left, right);              // read back the wheel speeds it set
}

int main(void) {
  struct {
    const char* word;
    long cls;
    int want_l;
    int want_r;
  } cases[] = {
      {"go", 1, 160, 160},
      {"stop", 4, 0, 0},
      {"left", 2, 40, 160},
      {"right", 3, 160, 40},
  };
  int fail = 0;
  for (const auto& c : cases) {
    int l = -1, r = -1;
    drive_with_class(c.cls, &l, &r);
    const bool ok = (l == c.want_l && r == c.want_r);
    printf("  VOICE()=\"%s\" (class %ld) -> MOVE %d %d (want %d %d)%s\n", c.word,
           c.cls, l, r, c.want_l, c.want_r, ok ? "" : "   <-- MISMATCH");
    if (!ok) fail = 1;
  }
  if (!fail)
    printf(
        "OK: firmware VM — the configurator's voice program drives the motors per "
        "keyword\n");
  else
    printf("FAIL: wrong motor output\n");
  return fail;
}
