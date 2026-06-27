// Host demo: run the SAME bytecode the IDE produces, on the PC, with no board.
// Proves the VM/ISA/missions are off-Arduino and ready for the Zephyr HAL.
#include <cstdio>
#include <string>
#include <vector>

#include "hal_host.h"
#include "runtime.h"

static void feedProgram(const std::vector<std::string>& lines) {
  for (const auto& l : lines) runtime::feedLine(l);
}

// Drive the simulated clock forward, ticking the VM, for `seconds` of sim time.
static void run(int seconds) {
  for (int i = 0; i < seconds * 10; i++) {  // +100 ms per step
    runtime::tick();
    host_advance(100);
  }
}

int main() {
  std::printf("=== textochip runtime — host build (native C++, no board) ===\n");
  runtime::init();

  // ── Demo 1: low-level semaforo.bas bytecode (red=4, yellow=2, green=1) ──
  std::printf("\n--- Demo 1: low-level semaforo bytecode (LED/WAIT/GOTO) ---\n");
  feedProgram({
      "LOAD",
      "MODE 4 OUT", "MODE 2 OUT", "MODE 1 OUT",
      "SET 4 1", "WAIT 5000", "SET 4 0",   // red 5s
      "SET 2 1", "WAIT 1000", "SET 2 0",   // yellow 1s
      "SET 1 1", "WAIT 5000", "SET 1 0",   // green 5s
      "JMP 3",                              // loop back to "SET 4 1"
      ".",
  });
  runtime::feedLine("RUN");
  run(23);  // ~2 cycles

  // ── Demo 2: the native MISSION (MISSION "SEMAFORO" -> CALL) ──
  std::printf("\n--- Demo 2: native MISSION \"SEMAFORO\" (CALL dispatch) ---\n");
  feedProgram({
      "LOAD",
      "CALL SEMAFORO 1 2 4 5 6",  // green=1 yellow=2 red=4 buzzer=5 button=6
      ".",
  });
  runtime::feedLine("RUN");
  run(15);  // ~1 pedestrian-crossing cycle (green->yellow->red+walk-beep)

  // ── Demo 3: parameterized MISSION (WITH green=3s yellow=1s red=2s) ──
  // The native Semaforo must honour + clamp the baked params: green should now
  // turn off at ~3000ms, yellow at ~4000ms, red at ~6000ms (vs the 6/2/5s defaults).
  std::printf(
      "\n--- Demo 3: MISSION \"SEMAFORO\" WITH green=3s yellow=1s red=2s "
      "beep=off button=off ---\n");
  feedProgram({
      "LOAD",
      "CALL SEMAFORO 1 2 4 5 6 green=3000 yellow=1000 red=2000 beep=off "
      "button=off mingreen=2000",
      ".",
  });
  runtime::feedLine("RUN");
  run(7);  // one faster cycle

  std::printf("\n=== done — same bytecode will run identically on ESP32 + nRF54L via Zephyr ===\n");
  return 0;
}
