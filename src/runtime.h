#pragma once
#include <string>

// The serial protocol handler + VM driver (brief §7), platform-agnostic.
// Both the host build and the Zephyr build call these:
//   init()        once at boot
//   pumpSerial()  + tick()  in the main loop
// The host demo also calls feedLine() directly to inject bytecode without stdin.
namespace runtime {
void init();                          // hal::init() + print "READY"
void feedLine(const std::string& l);  // process one protocol line (PING/LOAD/RUN/...)
void pumpSerial();                    // drain hal serial into lines -> feedLine()
void tick();                          // advance the VM one cooperative step-budget
}  // namespace runtime
