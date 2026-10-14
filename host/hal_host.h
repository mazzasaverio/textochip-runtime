#pragma once
#include <cstdint>
#include <string>

#include "../src/pads.h"

// Host-only controls (not part of the HAL): let main() drive a simulated clock
// and feed inputs so a whole program can be exercised instantly on the PC.
void host_advance(uint32_t ms);
void host_set_distance(int cm);  // simulated ultrasonic distance for DIST
int host_get_level(int pin);     // last written digital level (for tests)

// Edge-AI mic stub (for the voice tests): queue PCM samples that hal::aiCapture
// will drain, clear the queue, and read back the program's last MOVE wheel speeds.
void host_feed_audio(const int16_t* samples, int n);
// True when the stub queue is fully consumed — tests re-feed a word so the
// service's two-window debounce can see it twice.
bool host_audio_drained();
void host_reset_audio();
void host_feed_image(const unsigned char* pixels, int n);  // grayscale camera stub
void host_reset_image();
void host_feed_rgb(const unsigned char* pixels, int n);  // RGB camera frame stub
void host_reset_rgb();
bool host_get_move(int* left, int* right);  // false if the program never MOVEd

// Contract 3 (the pads tests): which board the host stands in for (PAD_HOST by
// default), the last line the firmware wrote to serial, and quiet mode (no pin
// trace on stdout, for tests that run thousands of programs).
void host_set_pad_board(PadBoard b);
const std::string& host_last_line();
void host_set_quiet(bool quiet);
// The buzzer's frequency (0 = off) and the servo's last angle (-1 = never),
// and a clean slate of pins, for tests that compare two runs.
int host_get_tone();
int host_get_servo();
void host_reset_pins();
// A sensor reading SENSE returns on the host ("bme280.temp" -> 23).
void host_set_sense(const std::string& key, int value);
