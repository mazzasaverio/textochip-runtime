#pragma once
#include <cstdint>
#include <string>

// The contract version this build speaks (SPEC.md), reported by CAPS. The IDE
// compares it with what a program needs before uploading.
#define TEXTOCHIP_CONTRACT 3

// Contract 3: a pin operand written `R<n>` names entry n of the program's
// resource table (`RES` lines, SPEC.md §1). It is stored in Instruction::a as
// RES_REF + n, far above any pin number; a bare number is a contract-2 logical
// pin, resolved by pads::legacyPad.
constexpr long RES_REF = 1L << 24;
inline bool isResRef(long a) { return a >= RES_REF; }

// Opcode set — mirrors lib/isa.ts and the brief §3. Strings are std::string
// (portable across the host g++ build and the Zephyr board build).
enum OpCode {
  OP_NOP = 0,
  // Tier 1 — core
  OP_MODE, OP_SET, OP_WAIT, OP_JMP, OP_CALL, OP_HALT,
  // Tier 2 — expressions & branching
  OP_PUSH, OP_LOAD, OP_STORE, OP_READ,
  OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_GT, OP_LT, OP_EQ, OP_AND, OP_NOT, OP_ABS,
  OP_JZ, OP_GOSUB, OP_RET,
  // Extensions (real hardware)
  OP_TONE, OP_RPIN, OP_SERVO, OP_AREAD,
  OP_MOVE,  // MOVE <left> <right> — differential drive, wheel speeds -255..255
  OP_DIST,  // DIST — push the ultrasonic distance in cm (HC-SR04; HAL-owned, no pin)
  // Tier 4 — edge-AI
  OP_AISTART,  // AISTART <model> — start the model's background inference service
  OP_INFER,    // INFER <model> — push the latest class index (0 = none), non-blocking
  // Contract 3 primitives
  OP_SENSE,    // SENSE <device> <channel> — push a sensor's reading (°C, %, hPa)
  OP_UNKNOWN
};

struct Instruction {
  OpCode op = OP_NOP;
  long a = 0;             // pin / addr / value / ms / var-index
  long b = 0;             // SET level; MODE dir (1=OUT, 0=IN pull-up, 2=IN pull-down)
  std::string missionId;  // CALL target
  int callArgs[6] = {0, 0, 0, 0, 0, 0};
  int callArgc = 0;
  std::string callParams;  // CALL key=value params (e.g. "green=4000 beep=fast")
};

const char* opcodeName(OpCode op);
OpCode opcodeFromName(const std::string& name);

// Parse one wire line into `out`. Returns false on unknown opcode / empty line.
bool parseInstructionLine(const std::string& line, Instruction& out);

// The program header lines of contract 3, which are not instructions:
//   CONTRACT <n>                          -> kind CONTRACT, number n
//   RES <id> <KIND> <pad> [pull=up|down]  -> kind RES, its fields
// Returns NOT_HEADER for any other line, BAD_HEADER for a malformed one.
struct HeaderLine {
  enum Kind { NOT_HEADER, BAD_HEADER, CONTRACT, RES } kind = NOT_HEADER;
  int number = 0;        // CONTRACT n, or the RES id
  std::string resKind;   // "DOUT", "DIN", "TONE", "SERVO", "ADC"
  std::string pad;       // "GPIO4", "P1.22", "D4"
  int pull = 0;          // DIN: 0 = up (the default), 2 = down (MODE's codes)
};
HeaderLine parseHeaderLine(const std::string& line);
