#include "vm.h"

#include <string>

#include "hal.h"

// Whenever the VM stops (STOP command, HALT, ran off the end, a fatal error),
// drive the physical actuators SAFE — above all the motors: a robot must not
// keep rolling after STOP (a real safety bug, not cosmetic). The buzzer is
// silenced too. The servo holds its last angle (there is no "off" and it's
// harmless). Pin 0 is a placeholder — the HAL owns the motor/buzzer pins.
static void stopActuators() {
  hal::move(0, 0);
  hal::toneOff(0);
}

void VM::reset() {
  clearProgram();
  vsp = 0;
  csp = 0;
  for (int i = 0; i < 26; i++) vars[i] = 0;
  aiClass = 0;
  visionClass = 0;
  visionX = 0;
  visionSize = 0;
}

void VM::clearProgram() {
  count = 0;
  resCount = 0;
  contract = 2;
  // A new program starts with fresh variables and stacks, as in the browser
  // simulator (simVm.ts load -> reset). They used to survive a LOAD, so a
  // program could read the last one's `a` (found by the contract-3
  // differential test, 2026-10-14).
  vsp = 0;
  csp = 0;
  for (int i = 0; i < 26; i++) vars[i] = 0;
  pc = 0;
  state = VM_IDLE;
  resumeAt = 0;
  currentMission = nullptr;
  aiActive = false;
  visionActive = false;
}

bool VM::addInstruction(const Instruction& in) {
  if (count >= MAX_PROGRAM) return false;
  program[count++] = in;
  return true;
}

std::string VM::setContract(int n) {
  if (n < 2 || n > TEXTOCHIP_CONTRACT)
    return "contract " + std::to_string(n) + " is not supported (this firmware speaks up to " +
           std::to_string(TEXTOCHIP_CONTRACT) + ")";
  contract = n;
  return "";
}

std::string VM::addResource(const HeaderLine& h) {
  if (h.number != resCount)
    return "RES " + std::to_string(h.number) + ": resources are numbered from 0 in order (expected " +
           std::to_string(resCount) + ")";
  if (resCount >= MAX_RESOURCES) return "too many resources";
  ResKind kind = pads::kindByName(h.resKind);
  if (kind == RES_UNKNOWN) return "R" + std::to_string(h.number) + ": unknown kind " + h.resKind;
  int pad = pads::byName(hal::padBoard(), h.pad);
  if (pad < 0) return "R" + std::to_string(h.number) + " " + h.pad + ": no such pad on this board";
  res[resCount++] = Resource{(int16_t)pad, (uint8_t)kind, (uint8_t)h.pull};
  contract = 3;
  return "";
}

int VM::padOf(long a) const {
  if (isResRef(a)) {
    long i = a - RES_REF;
    return i < resCount ? res[i].pad : -1;
  }
  return pads::legacyPad(hal::padBoard(), (int)a);
}

// The resource kind an opcode needs, and whether it reads a pin at all.
static bool usesPin(OpCode op) {
  return op == OP_MODE || op == OP_SET || op == OP_READ || op == OP_TONE ||
         op == OP_SERVO || op == OP_AREAD || op == OP_RPIN;
}

std::string VM::check() const {
  const PadBoard board = hal::padBoard();
  for (int i = 0; i < resCount; i++) {
    const std::string r = "R" + std::to_string(i) + " " + pads::name(board, res[i].pad);
    if (const char* why = pads::reserved(board, res[i].pad)) return r + ": " + why;
    for (int j = 0; j < i; j++)
      if (res[j].pad == res[i].pad) return r + ": already R" + std::to_string(j);
    const ResKind kind = (ResKind)res[i].kind;
    const int fixed = pads::fixedPad(board, kind);
    if (fixed >= 0 && fixed != res[i].pad)
      return r + ": this board's " + pads::kindName(kind) + " is wired to " + pads::name(board, fixed);
  }
  for (int i = 0; i < count; i++) {
    const Instruction& in = program[i];
    if (!usesPin(in.op)) continue;
    const std::string at = "instruction " + std::to_string(i) + " (" + opcodeName(in.op) + ")";
    if (!isResRef(in.a)) {
      if (contract >= 3) return at + ": a bare pin in a contract 3 program";
      continue;
    }
    const long r = in.a - RES_REF;
    if (r >= resCount) return at + ": R" + std::to_string(r) + " is not declared";
    const ResKind kind = (ResKind)res[r].kind;
    bool ok;
    switch (in.op) {
      case OP_SET: ok = kind == RES_DOUT; break;
      case OP_MODE:
        ok = in.b == 1 ? kind == RES_DOUT
                       : kind == RES_DIN && res[r].pull == (uint8_t)in.b;
        break;
      case OP_READ: ok = kind == RES_DIN || kind == RES_DOUT; break;
      case OP_TONE: ok = kind == RES_TONE; break;
      case OP_SERVO: ok = kind == RES_SERVO; break;
      case OP_AREAD: ok = kind == RES_ADC; break;
      default: ok = true; break;  // RPIN reads any pad
    }
    if (!ok) return at + ": R" + std::to_string(r) + " is a " + pads::kindName(kind) + " resource";
  }
  return "";
}

void VM::start() {
  pc = 0;
  state = VM_RUNNING;
}

void VM::stop() {
  state = VM_STOPPED;
  stopActuators();
  // Release the edge-AI services: aiActive/visionActive latch on the first
  // INFER and, without this, the mic/camera service kept running (and logging
  // detections) after STOP — bench-observed as phantom "voice?" lines on a
  // stopped program. The next RUN re-arms them via INFER.
  aiActive = false;
  visionActive = false;
}

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
      stopActuators();
      hal::serialWriteLine("OK: done (ran off end)");
      break;
    }
    Instruction& in = program[pc++];
    step(in);
  }
}

void VM::step(const Instruction& in) {
  switch (in.op) {
    case OP_MODE: {
      int p = padOf(in.a);
      if (p >= 0) hal::pinMode(p, (int)in.b);  // 0=IN pull-up, 1=OUT, 2=IN pull-down
      break;
    }
    case OP_SET: {
      int p = padOf(in.a);
      if (p >= 0) hal::pinWrite(p, in.b ? 1 : 0);
      break;
    }
    case OP_WAIT:
      resumeAt = hal::nowMs() + (uint32_t)in.a;
      state = VM_WAITING;
      break;
    case OP_JMP:
      pc = (int)in.a;
      break;
    case OP_HALT:
      state = VM_STOPPED;
      stopActuators();
      hal::serialWriteLine("OK: done");
      break;
    case OP_NOP:
      break;
    case OP_CALL: {
      // A mission's pins are contract-2 logical pins: hand it the pads.
      int pins[6];
      for (int k = 0; k < in.callArgc && k < 6; k++)
        pins[k] = pads::legacyPad(hal::padBoard(), in.callArgs[k]);
      currentMission = missionFor(in.missionId, pins, in.callArgc, in.callParams);
      if (currentMission != nullptr) {
        currentMission->begin();
        state = VM_RUNNING_MISSION;
      } else {
        hal::serialWriteLine(std::string("ERROR: unknown mission ") + in.missionId);
      }
      break;
    }

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
    case OP_READ: {
      int p = padOf(in.a);
      push(p >= 0 ? hal::pinRead(p) : 0);
      break;
    }
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
        stopActuators();
      }
      break;
    case OP_RET:
      if (csp > 0) pc = callStack[--csp];
      break;

    // ── Extensions ──
    case OP_TONE:
      if (in.b > 0) hal::tone(padOf(in.a), (int)in.b);
      else hal::toneOff(padOf(in.a));
      break;
    case OP_RPIN: {
      // Echo the operand as it was asked ("PIN 7", "PIN R2"), the board's level.
      int p = padOf(in.a);
      std::string asked = isResRef(in.a) ? "R" + std::to_string(in.a - RES_REF)
                                         : std::to_string((int)in.a);
      hal::serialWriteLine("PIN " + asked + " = " + std::to_string(p >= 0 ? hal::pinRead(p) : 0));
      break;
    }
    case OP_SERVO:  // SERVO <pin> <angle 0..180> — position a hobby servo (50 Hz PWM)
      hal::servo(padOf(in.a), (int)in.b);
      break;
    case OP_AREAD:  // AREAD <pin> — push the analog (ADC) reading
      push(hal::analogRead(padOf(in.a)));
      break;
    case OP_MOVE:  // MOVE <left> <right> — differential drive (L298N)
      hal::move((int)in.a, (int)in.b);
      break;
    case OP_DIST:  // DIST — push the ultrasonic distance in cm (HC-SR04)
      push(hal::distanceCm());
      break;
    case OP_PIXN:  // PIXN <count> — the strip's length for this program, all off
      hal::pixelsBegin((int)in.a);
      break;
    case OP_PIX: {  // PIX — colour on top, the pixel under it (-1 = all)
      const long rgb = pop();
      const long index = pop();
      hal::pixelSet((int)index, rgb);
      break;
    }
    case OP_DCLR:  // DCLR — every row of the screen empty
      hal::displayClear();
      break;
    case OP_DTEXT:  // DTEXT <row> "<text>" — the row is this text
      hal::displayText((int)in.a, in.callParams);
      break;
    case OP_DNUM:  // DNUM <row> — a number from the stack, after the row's text
      hal::displayText((int)in.a, std::to_string(pop()), true);
      break;
    case OP_SENSE:  // SENSE <device> <channel> — a sensor's reading (0 when absent)
      push(hal::sense(in.missionId, in.callParams));
      break;

    // ── Tier 4 — edge-AI ──
    case OP_AISTART:
      // Flag that the program wants a model running; the runtime's background
      // service (capture -> model -> setAiClass/setVisionClass) does the work.
      // Non-blocking. The <model> operand (in.missionId) picks voice vs vision.
      if (in.missionId == "vision") {
        visionActive = true;
      } else {
        aiActive = true;
      }
      break;
    case OP_INFER:
      // Push the latest class for <model> (0 = none). VOICE()="go" / SEE()="person"
      // compile to INFER <model> ; PUSH <idx> ; EQ. Executing INFER also flags the
      // model as wanted, so a program that uses VOICE()/SEE() starts its service on
      // its own — the compiler emits NO explicit AISTART. voice -> aiClass (mic),
      // vision -> visionClass (camera); each service fills its own register.
      // visionx / visionsize read WHERE and HOW BIG off that same camera frame
      // (SEEX() / SEESIZE()), so they arm the camera service the same way.
      if (in.missionId == "vision") {
        visionActive = true;
        push(visionClass);
      } else if (in.missionId == "visionx") {
        visionActive = true;
        push(visionX);
      } else if (in.missionId == "visionsize") {
        visionActive = true;
        push(visionSize);
      } else {
        aiActive = true;
        push(aiClass);
      }
      break;

    default:
      hal::serialWriteLine(std::string("WARN: opcode not implemented: ") + opcodeName(in.op));
      break;
  }
}
