// Contract 3 (SPEC.md §1): a program opens with a resource table and its pin
// operands name resources. Two questions, on each board's pad rules:
//   1. does the firmware refuse what it must (a reserved pad, a buzzer on a pad
//      with no PWM, an undeclared resource, a bare pin) and say why?
//   2. does a contract-3 program do exactly what the same program in contract 2
//      did? Thousands of generated programs, run both ways, compared pin by pin.
// Build & run:
//   make test-contract3
#include <cstdio>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "../src/pads.h"
#include "../src/runtime.h"
#include "hal_host.h"

static int fail = 0;
static void expect(const std::string& what, bool ok) {
  if (!ok) {
    printf("  FAIL %s\n", what.c_str());
    fail = 1;
  }
}

// Load a program through the serial protocol; the reply to "." is the verdict.
static std::string load(const std::vector<std::string>& lines) {
  runtime::feedLine("STOP");
  runtime::feedLine("LOAD");
  for (const auto& l : lines) runtime::feedLine(l);
  runtime::feedLine(".");
  return host_last_line();
}

static bool loaded(const std::string& reply) { return reply.rfind("OK: loaded", 0) == 0; }

static void rules() {
  host_set_pad_board(PAD_ESP32S3);
  expect("esp32: a plain program loads",
         loaded(load({"CONTRACT 3", "RES 0 DOUT GPIO4", "MODE R0 OUT", "SET R0 1"})));
  std::string r = load({"CONTRACT 3", "RES 0 DOUT GPIO19", "SET R0 1"});
  expect("esp32: GPIO19 is the USB: " + r, r == "ERROR: R0 GPIO19: the USB port");
  r = load({"RES 0 TONE GPIO7", "TONE R0 440"});
  expect("esp32: the buzzer only on its PWM pad: " + r,
         r == "ERROR: R0 GPIO7: this board's TONE is wired to GPIO5");
  expect("esp32: the buzzer on GPIO5 loads", loaded(load({"RES 0 TONE GPIO5", "TONE R0 440"})));
  r = load({"RES 0 DOUT GPIO4", "SET R1 1"});
  expect("an undeclared resource: " + r, r == "ERROR: instruction 0 (SET): R1 is not declared");
  r = load({"RES 0 DIN GPIO4", "SET R0 1"});
  expect("an input is not written: " + r, r == "ERROR: instruction 0 (SET): R0 is a DIN resource");
  r = load({"RES 0 DIN GPIO6 pull=up", "MODE R0 INPD"});
  expect("the pull follows the resource: " + r,
         r == "ERROR: instruction 0 (MODE): R0 is a DIN resource");
  expect("an output may be read back (LED(\"red\"))",
         loaded(load({"RES 0 DOUT GPIO4", "MODE R0 OUT", "READ R0"})));
  r = load({"CONTRACT 3", "RES 0 DOUT GPIO4", "SET 4 1"});
  expect("no bare pin in a contract 3 program: " + r,
         r == "ERROR: instruction 0 (SET): a bare pin in a contract 3 program");
  r = load({"RES 0 DOUT GPIO4", "RES 1 DOUT GPIO4"});
  expect("one pad, one resource: " + r, r == "ERROR: R1 GPIO4: already R0");
  r = load({"RES 1 DOUT GPIO4"});
  expect("resources are numbered in order: " + r,
         r == "ERROR: RES 1: resources are numbered from 0 in order (expected 0)");
  r = load({"RES 0 DOUT P1.22"});
  expect("a pad of another board: " + r, r == "ERROR: R0 P1.22: no such pad on this board");
  r = load({"CONTRACT 4"});
  expect("a newer contract: " + r,
         r == "ERROR: contract 4 is not supported (this firmware speaks up to 3)");
  expect("a contract 2 program still loads", loaded(load({"MODE 4 OUT", "SET 4 1"})));
  // A refused program is not kept: RUN must not run half of it.
  load({"RES 0 DOUT GPIO19", "SET R0 1"});
  runtime::feedLine("RUN");
  runtime::tick();
  expect("a refused program does not run", host_get_level(19) == 0);

  host_set_pad_board(PAD_NRF54LM20);
  expect("nrf: an LED on P1.22 loads",
         loaded(load({"RES 0 DOUT P1.22", "MODE R0 OUT", "SET R0 1"})));
  r = load({"RES 0 DOUT P1.01"});
  expect("nrf: P1.01 is shorted to ground: " + r,
         r == "ERROR: R0 P1.01: shorted to ground on the DK");
  r = load({"RES 0 DIN P1.14"});
  expect("nrf: the mic's pad: " + r,
         r == "ERROR: R0 P1.14: the microphone (its driver claims it at boot)");

  host_set_pad_board(PAD_UNOR4);
  expect("uno: an LED on D4 and a button on A1 load",
         loaded(load({"RES 0 DOUT D4", "RES 1 DIN A1 pull=up", "MODE R0 OUT", "MODE R1 IN"})));
  r = load({"RES 0 DOUT D10"});
  expect("uno: D10 is the ultrasonic's: " + r, r == "ERROR: R0 D10: the ultrasonic sensor");
  r = load({"RES 0 ADC A3", "AREAD R0"});
  expect("uno: the ADC is A0: " + r, r == "ERROR: R0 A3: this board's ADC is wired to A0");
}

// ── 2. the same program, two contracts ──────────────────────────────────────

struct Gen {
  PadBoard board;
  std::mt19937 rng;
  int pick(int n) { return (int)(rng() % (unsigned)n); }
};

enum Role { OUT, IN_UP, IN_DOWN };

// A random contract-2 program over the board's usable logical pins.
static std::vector<std::string> program(Gen& g, const std::vector<int>& logical,
                                        std::map<int, Role>& roles) {
  std::vector<std::string> p;
  std::vector<int> outs, ins;
  for (int l : logical) {
    Role r = (Role)g.pick(3);
    roles[l] = r;
    (r == OUT ? outs : ins).push_back(l);
    p.push_back("MODE " + std::to_string(l) + (r == OUT ? " OUT" : r == IN_UP ? " IN" : " INPD"));
  }
  const int body = 8 + g.pick(20);
  const int start = (int)p.size();
  for (int i = 0; i < body; i++) {
    switch (g.pick(9)) {
      case 0:
      case 1:
        if (!outs.empty())
          p.push_back("SET " + std::to_string(outs[g.pick((int)outs.size())]) + " " +
                      std::to_string(g.pick(2)));
        break;
      case 2:
        p.push_back("WAIT " + std::to_string(5 + g.pick(60)));
        break;
      case 3:
        if (!ins.empty() || !outs.empty()) {
          const auto& from = !ins.empty() && g.pick(2) ? ins : (outs.empty() ? ins : outs);
          p.push_back("READ " + std::to_string(from[g.pick((int)from.size())]));
          p.push_back("STORE " + std::string(1, (char)('a' + g.pick(3))));
        }
        break;
      case 4:
        p.push_back("LOAD " + std::string(1, (char)('a' + g.pick(3))));
        p.push_back("PUSH " + std::to_string(g.pick(3)));
        p.push_back("ADD");
        p.push_back("STORE " + std::string(1, (char)('a' + g.pick(3))));
        break;
      case 5:
        if (!outs.empty()) {
          // IF a THEN SET … : a conditional skip over one instruction
          p.push_back("LOAD " + std::string(1, (char)('a' + g.pick(3))));
          p.push_back("JZ " + std::to_string(p.size() + 2));
          p.push_back("SET " + std::to_string(outs[g.pick((int)outs.size())]) + " 1");
        }
        break;
      case 6:
        p.push_back("TONE 5 " + std::to_string(g.pick(2) ? 200 + g.pick(2000) : 0));
        break;
      case 7:
        p.push_back("SERVO 8 " + std::to_string(g.pick(181)));
        break;
      case 8:
        p.push_back("AREAD 9");
        p.push_back("STORE c");
        break;
    }
  }
  p.push_back("WAIT " + std::to_string(10 + g.pick(40)));
  p.push_back("JMP " + std::to_string(start));
  return p;
}

// The same program in contract 3, as the IDE lowers it (textochip
// lib/contract3.ts): resources in order of first use, pads by name.
static std::vector<std::string> lower(PadBoard board, const std::vector<std::string>& v2,
                                      const std::map<int, Role>& roles) {
  std::vector<std::string> res, body;
  std::map<int, int> rid;
  auto ref = [&](int logical, const std::string& kind) {
    auto it = rid.find(logical);
    if (it != rid.end()) return "R" + std::to_string(it->second);
    int id = (int)rid.size();
    rid[logical] = id;
    std::string line = "RES " + std::to_string(id) + " " + kind + " " +
                       pads::name(board, pads::legacyPad(board, logical));
    if (kind == "DIN") line += roles.at(logical) == IN_DOWN ? " pull=down" : " pull=up";
    res.push_back(line);
    return "R" + std::to_string(id);
  };
  for (const auto& l : v2) {
    size_t sp = l.find(' ');
    std::string op = l.substr(0, sp);
    std::string rest = sp == std::string::npos ? "" : l.substr(sp + 1);
    size_t sp2 = rest.find(' ');
    std::string a = rest.substr(0, sp2);
    std::string tail = sp2 == std::string::npos ? "" : rest.substr(sp2);
    std::string kind;
    if (op == "MODE" || op == "SET" || op == "READ") {
      int logical = std::stoi(a);
      kind = roles.count(logical) && roles.at(logical) != OUT ? "DIN" : "DOUT";
    } else if (op == "TONE") kind = "TONE";
    else if (op == "SERVO") kind = "SERVO";
    else if (op == "AREAD") kind = "ADC";
    if (kind.empty()) {
      body.push_back(l);
      continue;
    }
    body.push_back(op + " " + ref(std::stoi(a), kind) + tail);
  }
  // JMP/JZ targets count instructions only: the header lines add none.
  std::vector<std::string> out{"CONTRACT 3"};
  out.insert(out.end(), res.begin(), res.end());
  out.insert(out.end(), body.begin(), body.end());
  return out;
}

// Run a loaded program for a while; the state of every pad, the buzzer and the
// servo after each tick.
static std::vector<int> trace(const std::vector<int>& padsWatched) {
  std::vector<int> t;
  runtime::feedLine("RUN");
  for (int step = 0; step < 400; step++) {
    runtime::tick();
    host_advance(3);
    for (int p : padsWatched) t.push_back(host_get_level(p));
    t.push_back(host_get_tone());
    t.push_back(host_get_servo());
  }
  runtime::feedLine("STOP");
  return t;
}

static void equivalence(PadBoard board, const char* boardName, int programs) {
  host_set_pad_board(board);
  // The logical pins a program may use on this board: mapped by the legacy
  // map to a pad no rule reserves, and not one of the fixed peripherals' pins.
  std::vector<int> logical;
  for (int l : {1, 2, 4, 6, 7, 18, 38, 39, 40, 41, 42, 47, 3, 46}) {
    int p = pads::legacyPad(board, l);
    if (p >= 0 && !pads::reserved(board, p)) logical.push_back(l);
  }
  std::vector<int> watched;
  for (int l : logical) watched.push_back(pads::legacyPad(board, l));
  Gen g{board, std::mt19937(1234 + (unsigned)board)};
  int same = 0;
  for (int i = 0; i < programs; i++) {
    std::vector<int> use;
    for (int l : logical)
      if (g.pick(2)) use.push_back(l);
    std::map<int, Role> roles;
    auto v2 = program(g, use, roles);
    auto v3 = lower(board, v2, roles);
    host_reset_pins();
    std::string r2 = load(v2);
    auto t2 = trace(watched);
    host_reset_pins();
    std::string r3 = load(v3);
    auto t3 = trace(watched);
    if (!loaded(r2) || !loaded(r3)) {
      expect(std::string(boardName) + " program " + std::to_string(i) + " loads: " + r2 + " / " + r3,
             false);
      break;
    }
    if (t2 != t3) {
      expect(std::string(boardName) + " program " + std::to_string(i) + " runs the same", false);
      size_t k = 0;
      while (k < t2.size() && t2[k] == t3[k]) k++;
      const size_t per = watched.size() + 2;
      printf("    first difference at tick %zu, slot %zu: %d vs %d (slots: pads then tone, servo)\n",
             k / per, k % per, k < t2.size() ? t2[k] : -9, k < t3.size() ? t3[k] : -9);
      for (const auto& l : v2) printf("    v2 %s\n", l.c_str());
      for (const auto& l : v3) printf("    %s\n", l.c_str());
      break;
    }
    same++;
  }
  printf("  %s: %d of %d programs run the same in contract 2 and 3\n", boardName, same, programs);
}

int main(void) {
  host_set_quiet(true);
  rules();
  equivalence(PAD_ESP32S3, "esp32s3", 3000);
  equivalence(PAD_NRF54LM20, "nrf54lm20dk", 3000);
  equivalence(PAD_UNOR4, "unor4wifi", 3000);
  host_set_quiet(false);
  if (fail) {
    printf("FAIL: contract 3\n");
    return 1;
  }
  printf("OK: contract 3 — the pad rules hold on three boards, and generated programs run the same as in contract 2\n");
  return 0;
}
