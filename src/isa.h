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

// Opcode set: generated from src/isa.json (tools/gen_isa.py), the one list
// the IDE's lib/isa.json copies. OP_NOP is 0. Strings are std::string
// (portable across the host g++ build and the Zephyr board build).
#include "isa_ops.gen.h"
static_assert(TEXTOCHIP_ISA_CONTRACT == TEXTOCHIP_CONTRACT,
              "isa.json's contract and TEXTOCHIP_CONTRACT disagree");

enum OpCode {
#define TEXTOCHIP_OP_ENUM(name) OP_##name,
  TEXTOCHIP_OPCODES(TEXTOCHIP_OP_ENUM)
#undef TEXTOCHIP_OP_ENUM
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
