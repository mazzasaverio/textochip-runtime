// Firmware VM edge-AI opcode test: a bytecode program with AISTART/INFER runs on
// the real VM and branches on the AI class register. No TFLM, no mic — the class is
// injected with vm.setAiClass() (what the background inference service does on the
// board). This is the firmware twin of the product's simVm INFER test, and what
// `IF VOICE()="go" THEN ...` compiles to. Build & run:  make test-ai-vm
#include <cstdio>

#include "../src/isa.h"
#include "../src/vm.h"
#include "hal_host.h"

// 0 MODE 1 OUT  1 SET 1 0  2 AISTART voice  3 INFER voice  4 PUSH 1  5 EQ
// 6 JZ 9  7 SET 1 1  8 HALT  9 HALT      -> pin 1 is HIGH iff the heard class == 1
static const char* PROGRAM[] = {
    "MODE 1 OUT", "SET 1 0", "AISTART voice", "INFER voice", "PUSH 1",
    "EQ",         "JZ 9",    "SET 1 1",       "HALT",        "HALT",
};

static int run_with_class(long ai_class) {
  VM vm;
  vm.reset();
  for (const char* line : PROGRAM) {
    Instruction in;
    if (parseInstructionLine(line, in)) vm.addInstruction(in);
  }
  vm.setAiClass(ai_class);  // the inference service injects the detected class
  vm.start();
  for (int i = 0; i < 20 && vm.getState() != VM_STOPPED; i++) vm.tick();
  return host_get_level(1);
}

int main(void) {
  const int heard_go = run_with_class(1);    // class 1 -> branch -> pin 1 HIGH
  const int heard_other = run_with_class(2); // class 2 -> skip   -> pin 1 LOW
  printf("INFER=1 -> pin1=%d (want 1)   INFER=2 -> pin1=%d (want 0)\n",
         heard_go, heard_other);
  int fail = (heard_go != 1) || (heard_other != 0);
  if (!fail)
    printf("OK: firmware VM AISTART/INFER — bytecode -> AI class register -> branch\n");
  else
    printf("FAIL: unexpected branch\n");
  return fail;
}
