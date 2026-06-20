#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>

#include "hal.h"
#include "mission.h"

// Pedestrian-crossing traffic light — ported verbatim from the Arduino firmware's
// missions/Semaforo.h. The STATE MACHINE is unchanged; only the hardware calls now
// go through hal:: (pinMode/digitalWrite/digitalRead/tone/millis -> hal::*).
//
// `MISSION "SEMAFORO" [WITH …]` -> `CALL SEMAFORO <green> <yellow> <red>
// <buzzer> <button> [key=value …]`. The mission OWNS its guardrails: setParams()
// clamps every value to a safe range; the invariants (cycle order, one light at
// a time, beep only on red, press can't cut green below minGreen) are fixed here.
class Semaforo : public Mission {
 public:
  void configure(int green, int yellow, int red, int buzzer, int button) {
    greenPin = green;
    yellowPin = yellow;
    redPin = red;
    buzzerPin = buzzer;
    buttonPin = button;
  }

  // Parse + clamp the `key=value` params (e.g. "green=4000 beep=fast
  // button=off mingreen=3000"). Empty -> built-in defaults.
  void setParams(const std::string& params) {
    greenMs = 6000;
    yellowMs = 2000;
    redMs = 5000;
    minGreenMs = 3000;
    beepHalfMs = 300;  // "slow"
    buttonEnabled = true;

    std::string v;
    v = paramVal(params, "green");
    if (!v.empty()) greenMs = clampU(strtoul(v.c_str(), nullptr, 10), 2000, 15000);
    v = paramVal(params, "yellow");
    if (!v.empty()) yellowMs = clampU(strtoul(v.c_str(), nullptr, 10), 500, 3000);
    v = paramVal(params, "red");
    if (!v.empty()) redMs = clampU(strtoul(v.c_str(), nullptr, 10), 2000, 15000);
    v = paramVal(params, "mingreen");
    if (!v.empty()) minGreenMs = clampU(strtoul(v.c_str(), nullptr, 10), 2000, 10000);
    v = paramVal(params, "beep");
    if (!v.empty()) beepHalfMs = (v == "off") ? 0 : (v == "fast") ? 150 : 300;
    v = paramVal(params, "button");
    if (!v.empty()) buttonEnabled = (v != "off");

    // Invariant: a press can't cut green below the minimum green.
    if (minGreenMs > greenMs) minGreenMs = greenMs;
  }

  void begin() override {
    hal::pinMode(redPin, true);
    hal::pinMode(yellowPin, true);
    hal::pinMode(greenPin, true);
    // NOTE: do NOT gpio-configure the buzzer pin — the LEDC PWM (tone) owns it
    // (see app.overlay). Driving it as a plain GPIO would kill the square wave.
    hal::pinMode(buttonPin, false);  // INPUT_PULLUP
    enter(GREEN);
  }

  void tick() override {
    const uint32_t now = hal::nowMs();
    switch (state) {
      case GREEN:
        if (buttonEnabled && buttonPressed()) requested = true;
        // Immediate feedback: blink the green LED while a crossing is pending,
        // so a button press is visibly acknowledged at once.
        if (requested && now - blinkAt >= 250) {
          blinkAt = now;
          greenLit = !greenLit;
          hal::pinWrite(greenPin, greenLit ? 1 : 0);
        }
        if (now - phaseStart >= greenMs ||
            (requested && now - phaseStart >= minGreenMs)) {
          enter(YELLOW);
        }
        break;
      case YELLOW:
        if (now - phaseStart >= yellowMs) enter(RED);
        break;
      case RED:
        walkBeep(now);
        if (now - phaseStart >= redMs) {
          hal::toneOff(buzzerPin);
          enter(GREEN);
        }
        break;
    }
  }

  bool done() override { return false; }  // continuous

  // Active-low with INPUT_PULLUP: pressed reads LOW (0).
  bool buttonPressed() { return hal::pinRead(buttonPin) == 0; }

 private:
  enum Phase { GREEN, YELLOW, RED };
  static const int WALK_HZ = 1000;

  // Parameters (set/clamped by setParams; defaults below).
  uint32_t greenMs = 6000;     // max green with no request
  uint32_t minGreenMs = 2000;  // min green before a request is served (responsive)
  uint32_t yellowMs = 2000;
  uint32_t redMs = 5000;
  uint32_t beepHalfMs = 300;  // walk-beep half-period; 0 = beep off
  bool buttonEnabled = true;

  int greenPin = 1, yellowPin = 2, redPin = 4, buzzerPin = 5, buttonPin = 6;
  Phase state = GREEN;
  uint32_t phaseStart = 0;
  uint32_t beepAt = 0;
  bool beepOn = false;
  bool requested = false;
  uint32_t blinkAt = 0;
  bool greenLit = true;  // tracks the green LED while blinking a pending request

  static uint32_t clampU(unsigned long v, uint32_t lo, uint32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return (uint32_t)v;
  }

  // Value of `key=` in a space-separated param string. A leading-space sentinel
  // makes the match token-anchored, so "green=" doesn't match inside "mingreen=".
  static std::string paramVal(const std::string& params, const char* key) {
    std::string pp = " " + params;
    std::string k = std::string(" ") + key + "=";
    size_t i = pp.find(k);
    if (i == std::string::npos) return "";
    size_t start = i + k.size();
    size_t sp = pp.find(' ', start);
    return (sp == std::string::npos) ? pp.substr(start)
                                     : pp.substr(start, sp - start);
  }

  void enter(Phase p) {
    state = p;
    phaseStart = hal::nowMs();
    if (p == GREEN) {
      requested = false;
      greenLit = true;
      blinkAt = phaseStart;
    }
    hal::pinWrite(redPin, p == RED ? 1 : 0);
    hal::pinWrite(yellowPin, p == YELLOW ? 1 : 0);
    hal::pinWrite(greenPin, p == GREEN ? 1 : 0);
    if (p == RED) {
      beepAt = phaseStart;
      beepOn = false;
    }
  }

  void walkBeep(uint32_t now) {
    if (beepHalfMs == 0) return;  // beep off
    if (now - beepAt < beepHalfMs) return;
    beepAt = now;
    beepOn = !beepOn;
    if (beepOn) hal::tone(buzzerPin, WALK_HZ);
    else hal::toneOff(buzzerPin);
  }
};
