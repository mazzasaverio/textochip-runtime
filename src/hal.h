#pragma once
#include <cstdint>
#include <string>

// Hardware Abstraction Layer (HAL).
// The VM / ISA / missions are platform-agnostic and ONLY ever call hal::*.
// Each platform provides exactly one implementation of these functions:
//   host/hal_host.cpp          -> runs on your PC (what `make` builds & runs now)
//   zephyr/src/hal_zephyr.cpp  -> Zephyr: nRF54L (nRF Connect SDK) and ESP32 (upstream)
//
// This is the ONLY file that differs per board. Everything above it is identical.
namespace hal {

void init();  // bring up the clock + serial link

// GPIO. mode: 0 = INPUT_PULLUP (active-low, e.g. a button), 1 = OUTPUT,
// 2 = INPUT_PULLDOWN (active-high, e.g. a PIR — idles LOW when disconnected).
void pinMode(int pin, int mode);
void pinWrite(int pin, int level);  // level 0/1
int pinRead(int pin);               // returns 0/1
// Analog input (ADC) — returns a raw reading (e.g. 0..4095 on the ESP32-S3).
int analogRead(int pin);

// Buzzer (square wave)
void tone(int pin, int hz);  // hz>0 start
void toneOff(int pin);

// Hobby servo (e.g. SG90) — position to `angle` (0..180°) via a 50 Hz PWM frame.
void servo(int pin, int angle);

// Differential-drive motors (MOVE). left/right are signed wheel speeds, -255..255
// (sign = direction, magnitude = PWM duty). Drives a 2-motor L298N: the motor
// pins are a FIXED board wiring owned by the HAL (the MOVE opcode carries only
// the speeds), so the bytecode stays board-generic. 0,0 = stop.
void move(int left, int right);

// Microphone capture for the edge-AI voice tier. Reads up to `n` new mono samples
// (signed 16-bit PCM at the model's sample rate, ~16 kHz) from the board's I2S
// digital mic (e.g. INMP441) into `out`, and returns how many it wrote (0 if none
// ready). NON-BLOCKING — it drains only what the driver has already buffered, so it
// fits the cooperative tick loop; lazily starts capture on the first call. On a
// build with no mic (the host, or a board without one wired) it returns 0, so
// VOICE() stays 0 (none). The ONE per-board function the edge-AI tier adds — the
// background inference service (src/ai/ai_service.cpp) turns its samples into the
// class VOICE() reads (docs/edge-ai.md).
int aiCapture(int16_t* out, int n);

// Bench diagnostic for the mic path: a short human-readable status of the last
// capture-start attempt (device-ready, i2s_configure, i2s_trigger return codes,
// and whether capture is running), so the `MIC` command can report WHY audio is
// silent (config rejected vs clock not toggling) instead of just n=0. Host: a stub.
std::string micStatus();

// Bench diagnostic: drain one raw mic DMA block as interleaved 32-bit L/R words
// (L,R,L,R…) into `out` (up to `n`), returning the word count (0 if none ready).
// Unlike aiCapture (which keeps only the LEFT channel as int16) this exposes BOTH
// channels and the raw bits, so `MICRAW` can tell a left/right channel-select
// mismatch (L/R pin not grounded -> data on the right slot) from a dead data pin.
int aiCaptureRaw(int32_t* out, int n);

// Bench diagnostic: while capture runs, tight-loop sample the mic pads (SCK /
// FSYNC / SDIN) through the GPIO input buffer (readback does not steal the pad
// from the peripheral) and report toggle counts. Proves at the PAD whether the
// bit clock actually leaves the chip: sck toggles ≈ 0 while the driver is happily
// DMA-ing means the pin mux points at a pad the peripheral cannot drive (e.g. a
// GPIO port outside the peripheral's power domain on nRF54L) — firmware bug, not
// wiring. sck/ws toggling with sd flat = clock out, mic silent (wiring/mic side).
std::string micPinsProbe();

// Camera capture for the edge-AI VISION tier. Fills `out` with up to `max` bytes of a
// GRAYSCALE frame at the model's input size (e.g. 96x96 = 9216 px for person
// detection), one byte per pixel (0..255), and returns how many it wrote (0 if no
// frame ready). NON-BLOCKING. Board: an ESP32-S3-CAM over the DVP camera interface
// (Zephyr video subsystem). On a build with no camera (the host, or none wired) it
// returns 0, so SEE() stays 0 (nothing). The one per-board function the vision tier
// adds — the background vision service (src/ai/vision_service.cpp) turns its frame
// into the class SEE() reads. Mirrors aiCapture (mic), one sense apart.
int camCapture(uint8_t* out, int max);

// Non-volatile storage for the autorun program (brief §7). SAVE persists the
// raw bytecode text to flash; on boot, a saved program is loaded + run with no
// PC attached (the board's autonomy). One slot — a new save overwrites it.
bool storeSave(const std::string& program);  // persist; false on failure (e.g. too big)
bool storeLoad(std::string& out);            // true if a program is stored (out = its text)
void storeClear();                           // forget the saved program (disable autorun)

// Time
uint32_t nowMs();  // milliseconds since boot

// Serial link to the IDE (Web Serial sits on the other end)
int serialReadChar();                        // next byte, or -1 if none available
void serialWrite(const std::string& s);      // no trailing newline
void serialWriteLine(const std::string& s);  // appends '\n'

}  // namespace hal
