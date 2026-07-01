#include "runtime.h"

#include <cctype>
#include <string>

#include "ai/ai_service.h"
#include "hal.h"
#include "isa.h"
#include "vm.h"

namespace {
VM vm;
bool loading = false;
#ifdef TEXTOCHIP_AI
// True while the edge-AI listening service is running (a program has used VOICE()).
bool aiRunning = false;
#endif
std::string inbuf;
// The raw bytecode text of the loaded program, kept so SAVE can persist exactly
// what runs (and the boot autorun can re-feed it). Built up line-by-line on LOAD.
std::string programText;

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
      if (vm.addInstruction(in)) {
        programText += line;  // remember the raw text so SAVE persists it verbatim
        programText += '\n';
      } else {
        hal::serialWriteLine("ERROR: program full");
      }
    } else {
      hal::serialWriteLine("ERROR: bad instruction: " + line);
    }
    return;
  }

  if (line.empty()) return;

  if (line == "PING") {
    hal::serialWriteLine("PONG");
  } else if (line == "LOAD") {
    vm.stop();  // a boot-autorun program may be running — take over cleanly
    vm.clearProgram();
    programText.clear();
    loading = true;
    hal::serialWriteLine("OK: send program, end '.'");
  } else if (line == "RUN") {
    vm.start();
    hal::serialWriteLine("OK: running");
  } else if (line == "STOP") {
    vm.stop();
    hal::serialWriteLine("OK: stopped");
  } else if (line == "SAVE") {
    // Persist the loaded program to flash + arm autorun (brief §7): after this,
    // the board reruns it on every boot with no PC attached.
    if (programText.empty()) {
      hal::serialWriteLine("ERROR: nothing to save (LOAD a program first)");
    } else if (hal::storeSave(programText)) {
      hal::serialWriteLine("OK: saved");
    } else {
      hal::serialWriteLine("ERROR: save failed");
    }
  } else if (line == "CLEAR") {
    // Forget the saved program — the board boots idle again (no autorun).
    hal::storeClear();
    hal::serialWriteLine("OK: cleared");
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

void runtime::tick() {
#ifdef TEXTOCHIP_AI
  // Edge-AI: while a running program wants the model (it executed AISTART or any
  // INFER / VOICE()), drive the background inference service between VM ticks and
  // keep the VM's class register fresh — what INFER / VOICE() reads. The service
  // drains a bounded chunk of mic audio per call, so this never blocks; on a build
  // with no mic (or before a full window has been captured) poll() returns -1 and
  // the class is left untouched. Gated by TEXTOCHIP_AI so the plain host demo (and
  // any no-AI build) stays free of the model/feature deps; the board + the voice
  // tests define it. See src/ai/ai_service.cpp.
  if (vm.aiRequested()) {
    if (!aiRunning) {
      ai_service::reset();
      aiRunning = true;
    }
    int cls = ai_service::poll();
    if (cls >= 0) vm.setAiClass(cls);
  } else if (aiRunning) {
    aiRunning = false;  // program stopped / took a non-AI path — pause the service
  }
#endif
  vm.tick();
}

namespace {
// Parse a multi-line bytecode blob into the VM (used by boot autorun). Returns
// the number of instructions loaded. Mirrors the LOAD path, line by line.
int loadProgramText(const std::string& text) {
  vm.clearProgram();
  programText.clear();
  size_t start = 0;
  while (start < text.size()) {
    size_t nl = text.find('\n', start);
    size_t end = (nl == std::string::npos) ? text.size() : nl;
    std::string line = trim(text.substr(start, end - start));
    start = (nl == std::string::npos) ? text.size() : nl + 1;
    if (line.empty()) continue;
    Instruction in;
    if (parseInstructionLine(line, in) && vm.addInstruction(in)) {
      programText += line;
      programText += '\n';
    }
  }
  return vm.programSize();
}
}  // namespace

void runtime::init() {
  hal::init();
  hal::serialWriteLine("READY");

  // Autonomy (brief §7): if a program was SAVEd to flash, run it on boot with no
  // PC attached. The main loop still pumps serial, so an IDE can connect and
  // LOAD/STOP to take over at any time (the VM is cooperative).
  std::string saved;
  if (hal::storeLoad(saved) && loadProgramText(saved) > 0) {
    vm.start();
    hal::serialWriteLine("OK: autorun " + std::to_string(vm.programSize()));
  }
}
