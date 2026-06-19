// Zephyr entry point. Mirrors the Arduino setup()/loop(): same non-blocking design,
// the VM never busy-waits, so the device stays responsive to OVERRIDE/STOP.
#include <zephyr/kernel.h>

#include "runtime.h"

int main(void) {
  runtime::init();  // hal::init() + prints "READY"
  for (;;) {
    runtime::pumpSerial();  // drain bytes from the IDE -> protocol lines
    runtime::tick();        // advance the VM one cooperative step-budget
    k_msleep(1);            // yield (replaces the implicit Arduino loop cadence)
  }
  return 0;
}
