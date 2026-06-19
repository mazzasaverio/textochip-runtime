#include "isa.h"

#include <cctype>
#include <cstdlib>

static std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace((unsigned char)s[a])) a++;
  while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
  return s.substr(a, b - a);
}

static std::string upper(std::string s) {
  for (char& c : s) c = (char)std::toupper((unsigned char)c);
  return s;
}

static long toLong(const std::string& s) { return std::strtol(s.c_str(), nullptr, 10); }

const char* opcodeName(OpCode op) {
  switch (op) {
    case OP_NOP: return "NOP";
    case OP_MODE: return "MODE";
    case OP_SET: return "SET";
    case OP_WAIT: return "WAIT";
    case OP_JMP: return "JMP";
    case OP_CALL: return "CALL";
    case OP_HALT: return "HALT";
    case OP_PUSH: return "PUSH";
    case OP_LOAD: return "LOAD";
    case OP_STORE: return "STORE";
    case OP_READ: return "READ";
    case OP_ADD: return "ADD";
    case OP_SUB: return "SUB";
    case OP_MUL: return "MUL";
    case OP_DIV: return "DIV";
    case OP_GT: return "GT";
    case OP_LT: return "LT";
    case OP_EQ: return "EQ";
    case OP_AND: return "AND";
    case OP_NOT: return "NOT";
    case OP_ABS: return "ABS";
    case OP_JZ: return "JZ";
    case OP_GOSUB: return "GOSUB";
    case OP_RET: return "RET";
    case OP_TONE: return "TONE";
    case OP_RPIN: return "RPIN";
    default: return "?";
  }
}

OpCode opcodeFromName(const std::string& n) {
  static const struct {
    const char* k;
    OpCode v;
  } M[] = {
      {"NOP", OP_NOP},   {"MODE", OP_MODE}, {"SET", OP_SET},     {"WAIT", OP_WAIT},
      {"JMP", OP_JMP},   {"CALL", OP_CALL}, {"HALT", OP_HALT},   {"PUSH", OP_PUSH},
      {"LOAD", OP_LOAD}, {"STORE", OP_STORE}, {"READ", OP_READ}, {"ADD", OP_ADD},
      {"SUB", OP_SUB},   {"MUL", OP_MUL},   {"DIV", OP_DIV},     {"GT", OP_GT},
      {"LT", OP_LT},     {"EQ", OP_EQ},     {"AND", OP_AND},     {"NOT", OP_NOT},
      {"ABS", OP_ABS},   {"JZ", OP_JZ},     {"GOSUB", OP_GOSUB}, {"RET", OP_RET},
      {"TONE", OP_TONE}, {"RPIN", OP_RPIN},
  };
  for (auto& m : M)
    if (n == m.k) return m.v;
  return OP_UNKNOWN;
}

bool parseInstructionLine(const std::string& raw, Instruction& out) {
  std::string l = trim(raw);
  if (l.empty()) return false;

  size_t sp1 = l.find(' ');
  std::string opName = upper(sp1 == std::string::npos ? l : l.substr(0, sp1));
  std::string rest = sp1 == std::string::npos ? "" : trim(l.substr(sp1 + 1));

  out = Instruction{};
  out.op = opcodeFromName(opName);
  if (out.op == OP_UNKNOWN) return false;

  size_t sp2 = rest.find(' ');
  std::string t1 = sp2 == std::string::npos ? rest : rest.substr(0, sp2);
  std::string t2 = sp2 == std::string::npos ? "" : trim(rest.substr(sp2 + 1));

  switch (out.op) {
    case OP_MODE:
      out.a = toLong(t1);
      out.b = (upper(t2) == "OUT") ? 1 : 0;  // 1 = OUTPUT, 0 = INPUT_PULLUP
      break;
    case OP_SET:
    case OP_TONE:  // TONE <pin> <freq>
      out.a = toLong(t1);
      out.b = toLong(t2);
      break;
    case OP_WAIT:
    case OP_JMP:
    case OP_JZ:
    case OP_GOSUB:
    case OP_PUSH:
    case OP_READ:
    case OP_RPIN:
      out.a = toLong(t1);
      break;
    case OP_LOAD:
    case OP_STORE:
      if (!t1.empty()) out.a = (long)(std::tolower((unsigned char)t1[0]) - 'a');
      break;
    case OP_CALL: {
      out.missionId = t1;
      std::string rargs = t2;
      while (!rargs.empty() && out.callArgc < 6) {
        size_t sp = rargs.find(' ');
        std::string tok = sp == std::string::npos ? rargs : rargs.substr(0, sp);
        rargs = sp == std::string::npos ? "" : trim(rargs.substr(sp + 1));
        tok = trim(tok);
        if (!tok.empty()) out.callArgs[out.callArgc++] = (int)toLong(tok);
      }
      break;
    }
    default:
      break;  // HALT / NOP / ADD..RET take no operands
  }
  return true;
}
