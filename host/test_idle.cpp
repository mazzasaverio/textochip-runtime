// The power hook: the VM declares how long it will not need the CPU
// (VM::idleForMs), and runtime::idleMs() gates it behind everything that DOES
// need per-tick polling. The Zephyr main loop sleeps on this instead of
// spinning at 1 kHz through an hours-long WAIT (a scheduled pump program lives
// in VM_WAITING). Build & run:
//   make test-idle
#include <cstdio>

#include "../src/hal.h"
#include "../src/isa.h"
#include "../src/runtime.h"
#include "../src/vm.h"
#include "hal_host.h"

static int fail = 0;
static void check(const char *what, long got, long want) {
  printf("  %-44s got=%ld want=%ld\n", what, got, want);
  if (got != want) fail = 1;
}
static void checkRange(const char *what, long got, long lo, long hi) {
  printf("  %-44s got=%ld want=%ld..%ld\n", what, got, lo, hi);
  if (got < lo || got > hi) fail = 1;
}

int main(void) {
  // ── VM::idleForMs: the remaining WAIT, and 0 in every other state ──
  VM vm;
  vm.reset();
  Instruction in;
  parseInstructionLine("WAIT 30000", in);  // e.g. "pump for 30 s" wait
  vm.addInstruction(in);
  parseInstructionLine("JMP 0", in);
  vm.addInstruction(in);

  check("idle while IDLE (no program started)", vm.idleForMs(hal::nowMs()), 0);
  vm.start();
  check("idle while RUNNING (not ticked yet)", vm.idleForMs(hal::nowMs()), 0);
  vm.tick();  // executes WAIT 30000 -> VM_WAITING
  checkRange("idle right after WAIT 30000", vm.idleForMs(hal::nowMs()), 29900,
             30000);
  host_advance(10000);
  checkRange("idle 10 s into the wait", vm.idleForMs(hal::nowMs()), 19900,
             20000);
  host_advance(25000);  // past resumeAt — the clock passed it, tick not yet run
  check("idle once the wait has elapsed", vm.idleForMs(hal::nowMs()), 0);
  vm.stop();
  check("idle while STOPPED", vm.idleForMs(hal::nowMs()), 0);

  // ── runtime::idleMs: gated to 0 while a LOAD is streaming lines ──
  runtime::init();
  runtime::feedLine("LOAD");
  check("runtime idle mid-LOAD", (long)runtime::idleMs(), 0);
  runtime::feedLine("WAIT 60000");
  runtime::feedLine(".");
  check("runtime idle loaded but not running", (long)runtime::idleMs(), 0);
  runtime::feedLine("RUN");
  runtime::tick();  // executes the WAIT
  checkRange("runtime idle inside WAIT 60000", (long)runtime::idleMs(), 59900,
             60000);
  runtime::feedLine("STOP");
  check("runtime idle after STOP", (long)runtime::idleMs(), 0);

  printf(fail ? "FAIL\n" : "OK\n");
  return fail;
}
