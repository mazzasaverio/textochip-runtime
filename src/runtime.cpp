#include "runtime.h"

#include <cctype>
#include <string>

#include "hal.h"
#include "isa.h"
#include "vm.h"

namespace {
VM vm;
bool loading = false;
std::string inbuf;

std::string upper(std::string s) {
  for (char& c : s) c = (char)std::toupper((unsigned char)c);
  return s;
}
std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && std::isspace((unsigned char)s[a])) a++;
  while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
  return s.substr(a, b - a);
}
}  // namespace

void runtime::feedLine(const std::string& raw) {
  std::string line = trim(raw);

  if (loading) {
    if (line == ".") {
      loading = false;
      hal::serialWriteLine("OK: loaded " + std::to_string(vm.programSize()));
      return;
    }
    if (line.empty()) return;
    Instruction in;
    if (parseInstructionLine(line, in)) {
      if (!vm.addInstruction(in)) hal::serialWriteLine("ERROR: program full");
    } else {
      hal::serialWriteLine("ERROR: bad instruction: " + line);
    }
    return;
  }

  if (line.empty()) return;

  if (line == "PING") {
    hal::serialWriteLine("PONG");
  } else if (line == "LOAD") {
    vm.clearProgram();
    loading = true;
    hal::serialWriteLine("OK: send program, end '.'");
  } else if (line == "RUN") {
    vm.start();
    hal::serialWriteLine("OK: running");
  } else if (line == "STOP") {
    vm.stop();
    hal::serialWriteLine("OK: stopped");
  } else if (line == "SAVE") {
    hal::serialWriteLine("ERROR: SAVE + autorun arrive in firmware milestone 4");
  } else if (line.rfind("OVERRIDE", 0) == 0) {
    std::string rest = trim(line.substr(8));
    if (rest.empty()) {
      hal::serialWriteLine("ERROR: OVERRIDE needs an instruction");
      return;
    }
    if (upper(rest) == "STOP") {
      vm.stop();
      hal::serialWriteLine("OK: stopped");
      return;
    }
    Instruction in;
    if (parseInstructionLine(rest, in)) {
      vm.execOne(in);
      hal::serialWriteLine("OK");
    } else {
      hal::serialWriteLine("ERROR: bad instruction");
    }
  } else {
    hal::serialWriteLine("ERROR: unknown command: " + line);
  }
}

void runtime::pumpSerial() {
  int ci;
  while ((ci = hal::serialReadChar()) >= 0) {
    char c = (char)ci;
    if (c == '\n') {
      std::string l = inbuf;
      inbuf.clear();
      feedLine(l);
    } else if (c != '\r') {
      inbuf.push_back(c);
    }
  }
}

void runtime::tick() { vm.tick(); }

void runtime::init() {
  hal::init();
  hal::serialWriteLine("READY");
}
