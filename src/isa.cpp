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
    case OP_SERVO: return "SERVO";
    case OP_AREAD: return "AREAD";
    case OP_MOVE: return "MOVE";
    case OP_DIST: return "DIST";
    case OP_AISTART: return "AISTART";
    case OP_INFER: return "INFER";
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
      {"TONE", OP_TONE}, {"RPIN", OP_RPIN}, {"SERVO", OP_SERVO},
      {"AREAD", OP_AREAD}, {"MOVE", OP_MOVE}, {"DIST", OP_DIST},
      {"AISTART", OP_AISTART}, {"INFER", OP_INFER},
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
    case OP_MODE: {
      out.a = toLong(t1);
      std::string dir = upper(t2);
      // 1 = OUTPUT, 2 = INPUT_PULLDOWN (active-high sensors), 0 = INPUT_PULLUP.
      out.b = (dir == "OUT") ? 1 : (dir == "INPD") ? 2 : 0;
      break;
    }
    case OP_SET:
    case OP_TONE:   // TONE <pin> <freq>
    case OP_SERVO:  // SERVO <pin> <angle 0..180>
    case OP_MOVE:   // MOVE <left> <right> — wheel speeds (-255..255; toLong handles the sign)
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
    case OP_AREAD:  // AREAD <pin> — push the analog reading
      out.a = toLong(t1);
      break;
    case OP_LOAD:
    case OP_STORE: {
      // Variables are single letters a..z. Reject anything else so a bad token
      // can't become an out-of-range vars[] index in the VM.
      char c = t1.empty() ? '\0' : (char)std::tolower((unsigned char)t1[0]);
      if (c < 'a' || c > 'z') return false;
      out.a = (long)(c - 'a');
      break;
    }
    case OP_AISTART:
    case OP_INFER:
      // The model id (e.g. "voice") is a string operand; the firmware has one
      // baked-in model, so it's recorded but not otherwise used.
      out.missionId = t1;
      break;
    case OP_CALL: {
      out.missionId = t1;
      // After the id: integer pin tokens, then optional key=value param tokens
      // (e.g. "green=4000 beep=fast"). Pins -> callArgs; params -> callParams.
      std::string rargs = t2;
      while (!rargs.empty()) {
        size_t sp = rargs.find(' ');
        std::string tok = sp == std::string::npos ? rargs : rargs.substr(0, sp);
        rargs = sp == std::string::npos ? "" : trim(rargs.substr(sp + 1));
        tok = trim(tok);
        if (tok.empty()) continue;
        if (tok.find('=') != std::string::npos) {
          if (!out.callParams.empty()) out.callParams += ' ';
          out.callParams += tok;
        } else if (out.callArgc < 6) {
          out.callArgs[out.callArgc++] = (int)toLong(tok);
        }
      }
      break;
    }
    default:
      break;  // HALT / NOP / ADD..RET take no operands
  }
  return true;
}
