// DCLR, DTEXT <row> "<text>", DNUM <row> (SPEC.md §1): a small screen as rows
// of text, a number from the stack after a label, drawn once a tick. Here the
// host stands in for an SSD1306: a weather station prints its reading.
//   make test-display
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
  expect("parses DTEXT 0 \"Temp \" (its spaces kept)",
         parseInstructionLine("DTEXT 0 \"Temp \"", in) && in.op == OP_DTEXT && in.a == 0 &&
             in.callParams == "Temp ");
  expect("refuses a text without quotes", !parseInstructionLine("DTEXT 0 Temp", in));
  expect("refuses a text past 21 characters",
         !parseInstructionLine("DTEXT 0 \"0123456789012345678901\"", in));
  expect("parses DNUM 1", parseInstructionLine("DNUM 1", in) && in.op == OP_DNUM && in.a == 1);

  const char* program[] = {"DCLR",          "DTEXT 0 \"Meteo\"", "DTEXT 1 \"T \"",
                           "SENSE bme280 temp", "DNUM 1",      "WAIT 1000",
                           "JMP 2"};
  host_set_sense("bme280.temp", 23);
  runtime::feedLine("LOAD");
  for (const char* l : program) runtime::feedLine(l);
  runtime::feedLine(".");
  expect("loads", host_last_line() == "OK: loaded 7");
  runtime::feedLine("RUN");
  runtime::tick();
  expect("row 0 is the label", host_display_row(0) == "Meteo");
  expect("row 1 is the label and the reading", host_display_row(1) == "T 23");
  expect("rows 2 and 3 are empty", host_display_row(2).empty() && host_display_row(3).empty());
  runtime::feedLine("STOP");
  expect("CAPS lists display", runtime::capsLine().find(",display") != std::string::npos);
  if (fail) {
    printf("FAIL: DISPLAY\n");
    return 1;
  }
  printf("OK: DISPLAY — rows of text and a reading on the screen (a weather station)\n");
  return 0;
}
