#pragma once
#include <cstdint>

#include "isa.h"
#include "mission.h"

// Tick-based bytecode VM — brief §7. Hardware access goes through hal:: (see
// vm.cpp), so the same logic runs on the host and on any board. NO blocking delay.

#define MAX_PROGRAM 256
#define STEPS_PER_TICK 64
#define VALUE_STACK_SIZE 32
#define CALL_STACK_SIZE 16

enum VmState { VM_IDLE, VM_RUNNING, VM_WAITING, VM_RUNNING_MISSION, VM_STOPPED };

class VM {
 public:
  void reset();
  void clearProgram();
  bool addInstruction(const Instruction& in);  // false if program is full
  int programSize() const { return count; }

  void start();  // pc = 0, state = RUNNING
  void stop();   // state = STOPPED
  void tick();   // advance the VM (never blocks)
  void execOne(const Instruction& in);  // OVERRIDE: run one instruction now

  VmState getState() const { return state; }

  /** How many ms the VM will NOT need the CPU: the remaining time of a plain
   *  WAIT, and 0 in every other state (a running mission and the AI services
   *  are polled every tick; IDLE/STOPPED report 0 and let the caller pick its
   *  own idle cadence). This is the hook the power story hangs on: a scheduled
   *  program (`WAIT 6h` between pump runs) spends its life in VM_WAITING, and
   *  the main loop can hand that time to the kernel instead of spinning. */
  uint32_t idleForMs(uint32_t now) const {
    if (state != VM_WAITING) return 0;
    return resumeAt > now ? resumeAt - now : 0;
  }

  // Edge-AI (Tier 4). `INFER` pushes `aiClass` (0 = none), `AISTART` flags that
  // the program wants the model running. The background inference service (mic ->
  // features.c -> ai_infer) updates aiClass via setAiClass(); on the host or a
  // no-AI build it stays 0 unless injected (mirrors the simulator's "heard word").
  void setAiClass(long c) { aiClass = c; }
  bool aiRequested() const { return aiActive; }
  // Vision (SEE()) — a SEPARATE class register + wanted-flag, so a program can both
  // hear (voice) and see. INFER vision reads visionClass; the camera vision service
  // (src/ai/vision_service.cpp) fills it. Mirrors the voice register, one apart.
  void setVisionClass(long c) { visionClass = c; }
  // WHERE it is across the frame (0..100) and HOW BIG it looks (0..100), read by
  // INFER visionx / visionsize (SEEX() / SEESIZE()). One frame answers all three,
  // so the vision service fills them together with the class.
  void setVisionX(long x) { visionX = x; }
  void setVisionSize(long s) { visionSize = s; }
  bool visionRequested() const { return visionActive; }

 private:
  Instruction program[MAX_PROGRAM];
  int count = 0;
  int pc = 0;
  VmState state = VM_IDLE;
  uint32_t resumeAt = 0;

  long valueStack[VALUE_STACK_SIZE];
  int vsp = 0;
  int callStack[CALL_STACK_SIZE];
  int csp = 0;
  long vars[26] = {0};

  Mission* currentMission = nullptr;

  long aiClass = 0;       // latest edge-AI (voice) class index (0 = none)
  bool aiActive = false;  // a voice AISTART/INFER has run -> run the mic model
  long visionClass = 0;       // latest vision class index (0 = nothing seen)
  long visionX = 0;           // its horizontal centroid, 0..100 (0 when nothing seen)
  long visionSize = 0;        // its share of the frame, 0..100 (0 when nothing seen)
  bool visionActive = false;  // a vision AISTART/INFER has run -> run the camera model

  void push(long v);
  long pop();
  void step(const Instruction& in);
};
