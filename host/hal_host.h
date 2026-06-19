#pragma once
#include <cstdint>

// Host-only controls (not part of the HAL): let main() drive a simulated clock
// and a simulated button so a whole program can be exercised instantly on the PC.
void host_advance(uint32_t ms);
void host_set_button(int pin, bool pressed);
