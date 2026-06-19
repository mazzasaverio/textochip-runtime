#pragma once
#include <cstdint>

#include "hal.h"
#include "mission.h"

// Pedestrian-crossing traffic light — ported verbatim from the Arduino firmware's
// missions/Semaforo.h. The STATE MACHINE is unchanged; only the hardware calls now
// go through hal:: (pinMode/digitalWrite/digitalRead/tone/millis -> hal::*).
//
// `MISSION "SEMAFORO"` -> `CALL SEMAFORO <green> <yellow> <red> <buzzer> <button>`.
class Semaforo : public Mission {
 public:
  void configure(int green, int yellow, int red, int buzzer, int button) {
    greenPin = green;
    yellowPin = yellow;
    redPin = red;
    buzzerPin = buzzer;
    buttonPin = button;
  }

  void begin() override {
    hal::pinMode(redPin, true);
    hal::pinMode(yellowPin, true);
    hal::pinMode(greenPin, true);
    hal::pinMode(buzzerPin, true);
    hal::pinMode(buttonPin, false);  // INPUT_PULLUP
    enter(GREEN);
  }

  void tick() override {
    const uint32_t now = hal::nowMs();
    switch (state) {
      case GREEN:
        if (buttonPressed()) requested = true;
        if (now - phaseStart >= GREEN_MS ||
            (requested && now - phaseStart >= MIN_GREEN_MS)) {
          enter(YELLOW);
        }
        break;
      case YELLOW:
        if (now - phaseStart >= YELLOW_MS) enter(RED);
        break;
      case RED:
        walkBeep(now);
        if (now - phaseStart >= RED_MS) {
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
  static const uint32_t GREEN_MS = 6000;      // max green with no request
  static const uint32_t MIN_GREEN_MS = 3000;  // min green before a request is served
  static const uint32_t YELLOW_MS = 2000;
  static const uint32_t RED_MS = 5000;
  static const uint32_t BEEP_MS = 300;  // walk-beep half-period
  static const int WALK_HZ = 1000;

  int greenPin = 1, yellowPin = 2, redPin = 4, buzzerPin = 5, buttonPin = 6;
  Phase state = GREEN;
  uint32_t phaseStart = 0;
  uint32_t beepAt = 0;
  bool beepOn = false;
  bool requested = false;

  void enter(Phase p) {
    state = p;
    phaseStart = hal::nowMs();
    if (p == GREEN) requested = false;
    hal::pinWrite(redPin, p == RED ? 1 : 0);
    hal::pinWrite(yellowPin, p == YELLOW ? 1 : 0);
    hal::pinWrite(greenPin, p == GREEN ? 1 : 0);
    if (p == RED) {
      beepAt = phaseStart;
      beepOn = false;
    }
  }

  void walkBeep(uint32_t now) {
    if (now - beepAt < BEEP_MS) return;
    beepAt = now;
    beepOn = !beepOn;
    if (beepOn) hal::tone(buzzerPin, WALK_HZ);
    else hal::toneOff(buzzerPin);
  }
};
