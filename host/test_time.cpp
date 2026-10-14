// TIME: the milliseconds since the program started, so a program can measure
// how long something lasts (a stopwatch, a reflex game) without counting WAIT
// ticks. Build & run:
//   make test-time
#include <cstdio>

#include "../src/hal.h"
#include "../src/isa.h"
#include "../src/vm.h"
#include "hal_host.h"

static int fail = 0;
static void check(const char *what, long got, long want) {
  printf("  %-44s got=%ld want=%ld\n", what, got, want);
  if (got != want) fail = 1;
}

static void add(VM &vm, const char *line) {
  Instruction in;
  if (!parseInstructionLine(line, in)) {
    printf("  cannot parse: %s\n", line);
    fail = 1;
    return;
  }
  vm.addInstruction(in);
}

int main(void) {
  host_advance(5000);  // the board has been on for a while: TIME is not uptime
  VM vm;
  vm.reset();
  add(vm, "TIME");
  add(vm, "STORE a");
  add(vm, "WAIT 250");
  add(vm, "TIME");
  add(vm, "STORE b");
  add(vm, "HALT");
  check("TIME parses to OP_TIME", opcodeFromName("TIME"), OP_TIME);
  vm.start();
  vm.tick();  // TIME, STORE, WAIT 250
  check("TIME right after start", vm.var(0), 0);
  host_advance(250);
  vm.tick();  // TIME, STORE, HALT
  check("TIME after a 250 ms WAIT", vm.var(1), 250);
  printf(fail ? "FAIL\n" : "OK\n");
  return fail;
}
