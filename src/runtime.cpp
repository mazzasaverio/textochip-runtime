#include "runtime.h"

#include <cctype>
#include <string>

#include "ai/ai_service.h"
#include "ai/vision_service.h"
#include "hal.h"
#include "isa.h"
#include "vm.h"

namespace {
VM vm;
bool loading = false;
#ifdef TEXTOCHIP_AI
// True while the edge-AI listening service is running (a program has used VOICE()).
bool aiRunning = false;
// True while the camera vision service is running (a program has used SEE()).
bool visionRunning = false;
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

  // Silently drop lines carrying non-printable bytes: junk sits in the host's
  // line buffer when it opens the port (seen on the DK's J-Link VCOM), and the
  // IDE's flush newline turns it into a "line". It is never a real command —
  // replying "ERROR: unknown command: <echoed junk>" just splats � glyphs in
  // the IDE console.
  for (unsigned char c : line) {
    if (c < 0x20 || c > 0x7e) return;
  }

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
  } else if (line == "STORE?") {
    // Bench aid: report the autorun store state (is NVS mounted, how many bytes
    // are persisted, the sector geometry) so a SAVE that doesn't autorun on boot
    // can be diagnosed without a debugger.
    bool mounted = false;
    int bytes = 0, ss = 0, sc = 0;
    hal::storeStatus(&mounted, &bytes, &ss, &sc);
    hal::serialWriteLine("STORE mounted=" + std::to_string(mounted ? 1 : 0) +
                         " saved=" + std::to_string(bytes) + " sector=" +
                         std::to_string(ss) + "x" + std::to_string(sc));
  } else if (line == "MIC") {
    // Bench aid: sample the microphone over a ~600 ms WINDOW and report its
    // level, so you can confirm the mic is wired + clocking (speak -> the
    // numbers rise) BEFORE trusting the model. aiCapture is non-blocking and the
    // DMA fills a block only every ~16 ms, so a tight read loop mostly returns
    // n=0; we PACE the reads (yield ~10 ms between empty ones via nowMs) so a
    // single MIC call reliably accumulates real audio. Mean-abs level (no
    // sqrt/libm) — 0 = silence/not wired, rises with sound.
    int16_t buf[512];
    long peak = 0;
    long sumabs = 0;
    int total = 0;
    uint32_t start = hal::nowMs();
    while (hal::nowMs() - start < 600) {
      int n = hal::aiCapture(buf, 512);
      if (n <= 0) {
        uint32_t t = hal::nowMs();  // no block yet — wait ~10 ms for the DMA
        while (hal::nowMs() - t < 10) { /* spin */
        }
        continue;
      }
      for (int j = 0; j < n; j++) {
        int v = buf[j] < 0 ? -buf[j] : buf[j];
        if (v > peak) peak = v;
        sumabs += v;
      }
      total += n;
    }
    long level = total > 0 ? sumabs / total : 0;
    hal::serialWriteLine("OK: mic n=" + std::to_string(total) +
                         " peak=" + std::to_string(peak) +
                         " level=" + std::to_string(level) + " [" + hal::micStatus() + "]");
  } else if (line == "MICPINS") {
    // Bench aid: prove at the PAD whether the mic clocks actually leave the chip
    // (see hal::micPinsProbe). sck tog=0 while MIC happily DMAs zeros = the pin
    // mux points at a pad the peripheral can't drive — firmware, not wiring.
    hal::serialWriteLine("OK: micpins " + hal::micPinsProbe());
  } else if (line == "MICRAW") {
    // Bench aid: dump BOTH mic channels + the first raw words, to tell a left/right
    // channel-select mismatch (L/R pin not grounded -> the mic drives the R slot,
    // so the left channel we keep reads 0) from a genuinely dead data pin (all zero
    // on both). Same ~600 ms paced window as MIC.
    int32_t buf[512];
    long lpeak = 0, rpeak = 0;
    int total = 0;
    int32_t first[12];
    int nfirst = 0;
    uint32_t start = hal::nowMs();
    while (hal::nowMs() - start < 600) {
      int n = hal::aiCaptureRaw(buf, 512);
      if (n <= 0) {
        uint32_t t = hal::nowMs();
        while (hal::nowMs() - t < 10) { /* spin */
        }
        continue;
      }
      for (int j = 0; j + 1 < n; j += 2) {
        long l = buf[j] < 0 ? -(long)buf[j] : (long)buf[j];
        long r = buf[j + 1] < 0 ? -(long)buf[j + 1] : (long)buf[j + 1];
        if (l > lpeak) lpeak = l;
        if (r > rpeak) rpeak = r;
      }
      if (nfirst == 0) {
        for (int j = 0; j < n && nfirst < 12; j++) first[nfirst++] = buf[j];
      }
      total += n;
    }
    std::string raw;
    for (int j = 0; j + 1 < nfirst; j += 2) {
      raw += "(" + std::to_string(first[j]) + "," + std::to_string(first[j + 1]) + ") ";
    }
    hal::serialWriteLine("OK: micraw words=" + std::to_string(total) +
                         " Lpeak=" + std::to_string(lpeak) +
                         " Rpeak=" + std::to_string(rpeak) + " first: " + raw);
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
    // Feedback: announce what the board heard on the serial log, so the IDE's
    // Serial tab SHOWS it live. Without this a voice program is a black box —
    // you speak, nothing moves, and you can't tell a deaf mic from an
    // unrecognized word from an unpowered motor. Two levels:
    //   VOICE: go 78%          — accepted (top class over the confidence gate)
    //   voice? go 41% (<60%)   — heard but rejected by the gate (tuning view)
    // Only word classes print (background/quiet stays silent).
    if (cls >= 0) {
      static const char* kVoiceLabels[] = {"background", "go", "left", "right", "stop"};
      const int kNum = (int)(sizeof(kVoiceLabels) / sizeof(kVoiceLabels[0]));
      int top = -1;
      float conf = 0.0f;
      ai_service::lastTop(&top, &conf);
      if (top > 0) {
        const char* name = top < kNum ? kVoiceLabels[top] : "?";
        int pct = (int)(conf * 100.0f + 0.5f);
        if (cls > 0) {
          hal::serialWriteLine(std::string("VOICE: ") + name + " " + std::to_string(pct) + "%");
        } else if (ai_service::inRefractory()) {
          hal::serialWriteLine(std::string("voice? ") + name + " " + std::to_string(pct) +
                               "% (refractory)");
        } else {
          hal::serialWriteLine(std::string("voice? ") + name + " " + std::to_string(pct) +
                               "% (<" + std::to_string(ai_service::gatePct()) + "% or unconfirmed)");
        }
      }
      // Heartbeat every ~8 inferences (~2 s): mic level + the model's current top
      // guess. level=0 exposes a dead/unplugged mic instantly (a dead mic and a
      // confident "background" both print NO word lines — this line tells them
      // apart at a glance); speech pushes the level into the thousands.
      static int beat = 0;
      if (++beat >= 8) {
        beat = 0;
        int pct = (int)(conf * 100.0f + 0.5f);
        const char* name = (top > 0 && top < kNum) ? kVoiceLabels[top] : "background";
        int mfccMs = 0, inferMs = 0;
        ai_service::lastTiming(&mfccMs, &inferMs);
        int gx10 = ai_service::lastGainX10();
        hal::serialWriteLine("mic: level=" + std::to_string(ai_service::lastLevel()) +
                             " env=" + std::to_string(ai_service::lastEnvX1000()) +
                             " gain=" + std::to_string(gx10 / 10) + "." +
                             std::to_string(gx10 % 10) +
                             " spk=" + std::to_string(ai_service::lastSpikes()) + " top=" +
                             name + " " + std::to_string(pct) + "% mfcc=" +
                             std::to_string(mfccMs) + "ms infer=" + std::to_string(inferMs) +
                             "ms");
      }
    }
  } else if (aiRunning) {
    aiRunning = false;  // program stopped / took a non-AI path — pause the service
  }
  // Same for SEE() — the camera vision service fills the vision register (visionClass),
  // independent of the voice one, so a program can both hear and see.
  if (vm.visionRequested()) {
    if (!visionRunning) {
      vision_service::reset();
      visionRunning = true;
    }
    int vcls = vision_service::poll();
    if (vcls >= 0) {
      vm.setVisionClass(vcls);
      vm.setVisionX(vision_service::lastX());
      vm.setVisionSize(vision_service::lastSize());
    }
  } else if (visionRunning) {
    visionRunning = false;
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
