#pragma once
#include <cstdint>
#include <string>

// Opcode set — mirrors lib/isa.ts and the brief §3, IDENTICAL to the Arduino
// firmware's isa.h. The only change for portability: Arduino `String` -> std::string.
enum OpCode {
  OP_NOP = 0,
  // Tier 1 — core
  OP_MODE, OP_SET, OP_WAIT, OP_JMP, OP_CALL, OP_HALT,
  // Tier 2 — expressions & branching
  OP_PUSH, OP_LOAD, OP_STORE, OP_READ,
  OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_GT, OP_LT, OP_EQ, OP_AND, OP_NOT, OP_ABS,
  OP_JZ, OP_GOSUB, OP_RET,
  // Extensions (real hardware)
  OP_TONE, OP_RPIN,
  OP_UNKNOWN
};

struct Instruction {
  OpCode op = OP_NOP;
  long a = 0;             // pin / addr / value / ms / var-index
  long b = 0;             // SET level, MODE direction (1=OUT, 0=IN)
  std::string missionId;  // CALL target
  int callArgs[6] = {0, 0, 0, 0, 0, 0};
  int callArgc = 0;
};

const char* opcodeName(OpCode op);
OpCode opcodeFromName(const std::string& name);

// Parse one wire line into `out`. Returns false on unknown opcode / empty line.
bool parseInstructionLine(const std::string& line, Instruction& out);
