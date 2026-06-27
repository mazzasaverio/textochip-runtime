#include "vm.h"

#include <string>

#include "hal.h"

void VM::reset() {
  clearProgram();
  vsp = 0;
  csp = 0;
  for (int i = 0; i < 26; i++) vars[i] = 0;
}

void VM::clearProgram() {
  count = 0;
  pc = 0;
  state = VM_IDLE;
  resumeAt = 0;
  currentMission = nullptr;
}

bool VM::addInstruction(const Instruction& in) {
  if (count >= MAX_PROGRAM) return false;
  program[count++] = in;
  return true;
}

void VM::start() {
  pc = 0;
  state = VM_RUNNING;
}

void VM::stop() { state = VM_STOPPED; }

void VM::execOne(const Instruction& in) { step(in); }

void VM::push(long v) {
  if (vsp < VALUE_STACK_SIZE) valueStack[vsp++] = v;
}

long VM::pop() { return vsp > 0 ? valueStack[--vsp] : 0; }

void VM::tick() {
  if (state == VM_WAITING) {
    if (hal::nowMs() < resumeAt) return;
    state = VM_RUNNING;
  }
  if (state == VM_RUNNING_MISSION) {
    if (currentMission != nullptr) {
      currentMission->tick();
      if (!currentMission->done()) return;
    }
    state = VM_RUNNING;
  }

  int budget = STEPS_PER_TICK;
  while (budget-- > 0 && state == VM_RUNNING) {
    if (pc < 0 || pc >= count) {
      state = VM_STOPPED;
      hal::serialWriteLine("OK: done (ran off end)");
      break;
    }
    Instruction& in = program[pc++];
    step(in);
  }
}

void VM::step(const Instruction& in) {
  switch (in.op) {
    case OP_MODE:
      hal::pinMode((int)in.a, (int)in.b);  // 0=IN pull-up, 1=OUT, 2=IN pull-down
      break;
    case OP_SET:
      hal::pinWrite((int)in.a, in.b ? 1 : 0);
      break;
    case OP_WAIT:
      resumeAt = hal::nowMs() + (uint32_t)in.a;
      state = VM_WAITING;
      break;
    case OP_JMP:
      pc = (int)in.a;
      break;
    case OP_HALT:
      state = VM_STOPPED;
      hal::serialWriteLine("OK: done");
      break;
    case OP_NOP:
      break;
    case OP_CALL:
      currentMission =
          missionFor(in.missionId, in.callArgs, in.callArgc, in.callParams);
      if (currentMission != nullptr) {
        currentMission->begin();
        state = VM_RUNNING_MISSION;
      } else {
        hal::serialWriteLine(std::string("ERROR: unknown mission ") + in.missionId);
      }
      break;

    // ── Tier 2 ──
    case OP_PUSH: push(in.a); break;
    // Variables are a..z (indices 0..25). Bounds-check before indexing vars[]:
    // a corrupt saved program or a stray OVERRIDE must never read/write out of
    // bounds on an MCU with no memory protection (the parser also rejects a
    // non-a..z variable, so this is defense in depth).
    case OP_LOAD: push((in.a >= 0 && in.a < 26) ? vars[in.a] : 0); break;
    case OP_STORE: {
      long v = pop();  // pop regardless, to keep the value stack balanced
      if (in.a >= 0 && in.a < 26) vars[in.a] = v;
    } break;
    case OP_READ: push(hal::pinRead((int)in.a)); break;
    case OP_ADD: { long b = pop(); push(pop() + b); } break;
    case OP_SUB: { long b = pop(); push(pop() - b); } break;
    case OP_MUL: { long b = pop(); push(pop() * b); } break;
    case OP_DIV: { long b = pop(); long a = pop(); push(b != 0 ? a / b : 0); } break;
    case OP_GT: { long b = pop(); push(pop() > b ? 1 : 0); } break;
    case OP_LT: { long b = pop(); push(pop() < b ? 1 : 0); } break;
    case OP_EQ: { long b = pop(); push(pop() == b ? 1 : 0); } break;
    case OP_AND: { long b = pop(); long a = pop(); push((a && b) ? 1 : 0); } break;
    case OP_NOT: push(pop() ? 0 : 1); break;
    case OP_ABS: { long a = pop(); push(a < 0 ? -a : a); } break;
    case OP_JZ:
      if (pop() == 0) pc = (int)in.a;
      break;
    case OP_GOSUB:
      // On a full call stack, do NOT jump without pushing — that would make a
      // later RET pop a stale address and corrupt control flow silently. Stop
      // with a clear error instead (deeply/infinitely nested GOSUB).
      if (csp < CALL_STACK_SIZE) {
        callStack[csp++] = pc;
        pc = (int)in.a;
      } else {
        hal::serialWriteLine("ERROR: call stack overflow");
        state = VM_STOPPED;
      }
      break;
    case OP_RET:
      if (csp > 0) pc = callStack[--csp];
      break;

    // ── Extensions ──
    case OP_TONE:
      if (in.b > 0) hal::tone((int)in.a, (int)in.b);
      else hal::toneOff((int)in.a);
      break;
    case OP_RPIN:
      hal::serialWriteLine("PIN " + std::to_string((int)in.a) + " = " +
                           std::to_string(hal::pinRead((int)in.a)));
      break;
    case OP_SERVO:  // SERVO <pin> <angle 0..180> — position a hobby servo (50 Hz PWM)
      hal::servo((int)in.a, (int)in.b);
      break;
    case OP_AREAD:  // AREAD <pin> — push the analog (ADC) reading
      push(hal::analogRead((int)in.a));
      break;

    default:
      hal::serialWriteLine(std::string("WARN: opcode not implemented: ") + opcodeName(in.op));
      break;
  }
}
