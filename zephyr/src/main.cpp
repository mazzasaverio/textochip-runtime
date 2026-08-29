// Zephyr entry point. Mirrors the Arduino setup()/loop(): same non-blocking design,
// the VM never busy-waits, so the device stays responsive to OVERRIDE/STOP.
#include <zephyr/kernel.h>

#include "runtime.h"

// How long one sleep may last while the VM idles inside a WAIT. This is the
// worst-case latency on a serial command (STOP/OVERRIDE arriving mid-sleep sits
// in the UART buffer until the next pump), so it stays small enough to feel
// immediate at the IDE — while still cutting the loop from ~1 kHz to ~20 Hz
// across the hours-long WAITs of a scheduled program (`WAIT 6h` between pump
// runs), which is what hands the kernel real idle windows. Measured power
// (CONFIG_PM residencies, light sleep) is bench work on top of this hook, not
// part of it — see the battery ADR in the product repo's docs/decisions.md.
static const uint32_t IDLE_CHUNK_MS = 50;

int main(void) {
  runtime::init();  // hal::init() + prints "READY"
  for (;;) {
    runtime::pumpSerial();  // drain bytes from the IDE -> protocol lines
    runtime::tick();        // advance the VM one cooperative step-budget
    // Sleep for as much of the VM's declared idle time as latency allows;
    // 1 ms floor keeps the Arduino-loop cadence whenever the VM is busy.
    uint32_t idle = runtime::idleMs();
    if (idle > IDLE_CHUNK_MS) idle = IDLE_CHUNK_MS;
    k_msleep(idle > 0 ? (int32_t)idle : 1);
  }
  return 0;
}
