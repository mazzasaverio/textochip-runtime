#pragma once
#include <cstdint>

// Host-only controls (not part of the HAL): let main() drive a simulated clock
// and a simulated button so a whole program can be exercised instantly on the PC.
void host_advance(uint32_t ms);
void host_set_button(int pin, bool pressed);
void host_set_analog(int pin, int value);  // simulated ADC reading for AREAD
int host_get_level(int pin);               // last written digital level (for tests)

// Edge-AI mic stub (for the voice tests): queue PCM samples that hal::aiCapture
// will drain, clear the queue, and read back the program's last MOVE wheel speeds.
void host_feed_audio(const int16_t* samples, int n);
void host_reset_audio();
void host_feed_image(const unsigned char* pixels, int n);  // camera frame stub
void host_reset_image();
bool host_get_move(int* left, int* right);  // false if the program never MOVEd
