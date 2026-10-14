// SENSE <device> <channel> (SPEC.md §1): a sensor's reading through the
// board's driver, pushed like AREAD. Here the host stands in for a BME280:
// a thermostat program lights its LED above 25 °C.
//   make test-sense
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
  expect("parses SENSE bme280 temp",
         parseInstructionLine("SENSE bme280 temp", in) && in.op == OP_SENSE &&
             in.missionId == "bme280" && in.callParams == "temp");
  expect("refuses a SENSE without a channel", !parseInstructionLine("SENSE bme280", in));
  expect("refuses a channel that is not a word",
         !parseInstructionLine("SENSE bme280 t.e", in));

  // 10 SENSE bme280 temp / PUSH 25 / GT / JZ 6 / SET 4 1 / JMP 0 / SET 4 0 / JMP 0
  const char* program[] = {"MODE 4 OUT", "SENSE bme280 temp", "PUSH 25", "GT", "JZ 7",
                           "SET 4 1",    "JMP 1",             "SET 4 0", "JMP 1"};
  runtime::feedLine("LOAD");
  for (const char* l : program) runtime::feedLine(l);
  runtime::feedLine(".");
  expect("loads", host_last_line() == "OK: loaded 9");
  runtime::feedLine("RUN");
  host_set_sense("bme280.temp", 22);
  for (int i = 0; i < 5; i++) runtime::tick();
  expect("22 °C: the LED stays off", host_get_level(4) == 0);
  host_set_sense("bme280.temp", 28);
  for (int i = 0; i < 5; i++) runtime::tick();
  expect("28 °C: the LED lights", host_get_level(4) == 1);
  runtime::feedLine("STOP");
  // a second device is the same opcode with another word: a DHT22's humidity
  const char* humid[] = {"MODE 4 OUT", "SENSE dht22 hum", "PUSH 70", "GT", "JZ 7",
                         "SET 4 1",    "JMP 1",           "SET 4 0", "JMP 1"};
  runtime::feedLine("LOAD");
  for (const char* l : humid) runtime::feedLine(l);
  runtime::feedLine(".");
  runtime::feedLine("RUN");
  host_set_sense("dht22.hum", 80);
  for (int i = 0; i < 5; i++) runtime::tick();
  expect("a DHT22 at 80 %: the LED lights", host_get_level(4) == 1);
  host_set_sense("dht22.hum", 40);
  for (int i = 0; i < 5; i++) runtime::tick();
  expect("40 %: off again", host_get_level(4) == 0);
  runtime::feedLine("STOP");
  expect("CAPS lists the sensor", runtime::capsLine().find(" sensors=bme280") != std::string::npos);
  if (fail) {
    printf("FAIL: SENSE\n");
    return 1;
  }
  printf("OK: SENSE — a sensor reading drives the program (a thermostat, a hygrostat)\n");
  return 0;
}
