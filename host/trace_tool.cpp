// The firmware VM as a trace machine, for the differential test against the
// browser simulator (textochip lib/differential.test.ts). Reads programs from
// stdin, runs each in the host VM with the board's pad rules, and prints one
// trace line per program. Input, per program:
//
//   BOARD esp32s3 | nrf54lm20dk | unor4wifi
//   WATCH <pad> <pad> …          the output pads to sample, in order
//   <program lines, any contract>
//   END
//
// Output, per program: `OK <samples>` or `REFUSED <the firmware's reason>`.
// A sample per tick (400 ticks, 3 ms apart): each watched pad's level, then
// the buzzer's frequency and the servo's angle (-1 = never set).
// Build: make -C host trace-tool
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "../src/pads.h"
#include "../src/runtime.h"
#include "hal_host.h"

static PadBoard boardByName(const std::string& id) {
  if (id == "esp32s3") return PAD_ESP32S3;
  if (id == "nrf54lm20dk") return PAD_NRF54LM20;
  if (id == "unor4wifi") return PAD_UNOR4;
  return PAD_HOST;
}

int main() {
  host_set_quiet(true);
  std::string line, boardId;
  std::vector<std::string> watch, program;
  while (std::getline(std::cin, line)) {
    if (line.rfind("BOARD ", 0) == 0) {
      boardId = line.substr(6);
      watch.clear();
      program.clear();
    } else if (line.rfind("WATCH", 0) == 0) {
      std::string rest = line.size() > 6 ? line.substr(6) : "";
      size_t pos = 0;
      while (pos < rest.size()) {
        size_t sp = rest.find(' ', pos);
        if (sp == std::string::npos) sp = rest.size();
        if (sp > pos) watch.push_back(rest.substr(pos, sp - pos));
        pos = sp + 1;
      }
    } else if (line == "END") {
      const PadBoard board = boardByName(boardId);
      host_set_pad_board(board);
      host_reset_pins();
      runtime::feedLine("STOP");
      runtime::feedLine("LOAD");
      for (const auto& l : program) runtime::feedLine(l);
      runtime::feedLine(".");
      const std::string reply = host_last_line();
      if (reply.rfind("OK: loaded", 0) != 0) {
        std::printf("REFUSED %s\n", reply.c_str());
        continue;
      }
      std::vector<int> padsWatched;
      for (const auto& w : watch) padsWatched.push_back(pads::byName(board, w));
      std::string out = "OK";
      runtime::feedLine("RUN");
      for (int step = 0; step < 400; step++) {
        runtime::tick();
        host_advance(3);
        for (int p : padsWatched) out += " " + std::to_string(host_get_level(p));
        out += " " + std::to_string(host_get_tone());
        out += " " + std::to_string(host_get_servo());
      }
      runtime::feedLine("STOP");
      std::printf("%s\n", out.c_str());
    } else {
      program.push_back(line);
    }
  }
  return 0;
}
