// PIXN <count> and PIX (SPEC.md §1): an addressable LED strip, its colours set
// by index from the stack, one frame sent per tick. Here the host stands in for
// a WS2812 strip: a chaser lights one pixel after another.
//   make test-pixels
#include <cstdio>
#include <string>

#include "../src/isa.h"
#include "../src/runtime.h"
#include "hal_host.h"

static int fail = 0;
static void expect(const std::string& what, bool ok) {
  printf("  %-60s %s\n", what.c_str(), ok ? "ok" : "FAIL");
  if (!ok) fail = 1;
}

int main() {
  host_set_quiet(true);
  Instruction in;
  expect("parses PIXN 8", parseInstructionLine("PIXN 8", in) && in.op == OP_PIXN && in.a == 8);
  expect("parses PIX", parseInstructionLine("PIX", in) && in.op == OP_PIX);

  // all red, then pixel 2 blue
  const char* program[] = {"PIXN 8", "PUSH -1", "PUSH 4194304", "PIX", "PUSH 2",
                           "PUSH 64", "PIX", "PUSH 99", "PUSH 255", "PIX", "HALT"};
  runtime::feedLine("LOAD");
  for (const char* l : program) runtime::feedLine(l);
  runtime::feedLine(".");
  expect("loads", host_last_line() == "OK: loaded 11");
  const int before = host_pixel_frames();
  runtime::feedLine("RUN");
  for (int i = 0; i < 10; i++) runtime::tick();
  const auto& px = host_pixels();
  expect("the strip has the program's 8 pixels", px.size() == 8);
  expect("all red, then pixel 2 blue",
         px.size() == 8 && px[0] == 0x400000 && px[2] == 0x40 && px[7] == 0x400000);
  expect("pixel 99 of 8 is ignored", px.size() == 8);
  const int frames = host_pixel_frames() - before;
  expect("one frame for one tick's changes, none while nothing changes", frames == 1);

  // a chaser: i = 0..7, pixel i white, wait, off
  const char* chaser[] = {"PIXN 8", "PUSH 0",  "STORE i", "LOAD i",  "PUSH 4210752", "PIX",
                          "WAIT 100", "LOAD i", "PUSH 0",  "PIX",     "LOAD i",       "PUSH 1",
                          "ADD",     "STORE i", "LOAD i",  "PUSH 8",  "LT",           "JZ 1",
                          "JMP 3"};
  runtime::feedLine("STOP");
  runtime::feedLine("LOAD");
  for (const char* l : chaser) runtime::feedLine(l);
  runtime::feedLine(".");
  runtime::feedLine("RUN");
  runtime::tick();
  expect("the chaser lights pixel 0 first", host_pixels().size() == 8 && host_pixels()[0] == 0x404040);
  runtime::feedLine("STOP");
  expect("CAPS lists pixels", runtime::capsLine().find(",pixels") != std::string::npos);
  if (fail) {
    printf("FAIL: PIXELS\n");
    return 1;
  }
  printf("OK: PIXELS — a strip coloured by index, one frame a tick (all red, a chaser)\n");
  return 0;
}
