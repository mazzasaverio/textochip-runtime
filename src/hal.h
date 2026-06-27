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
