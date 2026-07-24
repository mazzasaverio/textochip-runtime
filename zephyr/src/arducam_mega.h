/*
 * Arducam Mega (SPI) — the two entry points the rest of the firmware needs.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TEXTOCHIP_ARDUCAM_MEGA_H
#define TEXTOCHIP_ARDUCAM_MEGA_H

#include <stdint.h>

#include <string>

// Fill `out` with up to `max` bytes of the current 96x96 RGB888 frame, resuming
// where the last call stopped. Returns the bytes written, or 0 while the camera
// is still exposing (and always 0 when no camera is wired). This IS the body of
// hal::camCaptureRGB — the same cooperative, never-blocking contract.
int arducam_capture_rgb(uint8_t* out, int max);

// Bench aid behind the CAM serial command: what the camera says about itself
// (sensor id, firmware date) and how many bytes one frame actually measures.
std::string arducam_probe();

#endif  // TEXTOCHIP_ARDUCAM_MEGA_H
