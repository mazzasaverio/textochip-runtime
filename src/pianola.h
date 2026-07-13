#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>

#include "hal.h"
#include "mission.h"

// Playable one-button keyboard (brief §8). Each note sounds on the buzzer and
// lights one of the three LEDs as a visual keyboard. In `manual` mode a button
// press plays the next note of the scale; in `auto` mode the mission walks the
// scale by itself. All hardware access via hal::*, so the same code runs on the
// host build and on any board — the TS twin is lib/backends/missions.ts.
//
// `MISSION "PIANOLA" [WITH …]` -> `CALL PIANOLA <green> <yellow> <red> <buzzer>
// <button> [key=value …]`. The mission OWNS its guardrails: setParams()
// selects/clamps every value; the invariants (monophonic, the scale's fixed
// intervals, a safe Hz range) are fixed here.
class Pianola : public Mission {
 public:
  void configure(int green, int yellow, int red, int buzzer, int button) {
    greenPin = green;
    yellowPin = yellow;
    redPin = red;
    buzzerPin = buzzer;
    buttonPin = button;
  }

  // Parse + clamp the `key=value` params (e.g. "scale=blues note=200 octave=high
  // mode=auto"). Empty -> built-in defaults.
  void setParams(const std::string& params) {
    noteMs = 300;
    octave = MID;
    autoPlay = false;
    setScale("major");

    std::string v;
    v = paramVal(params, "scale");
    if (!v.empty()) setScale(v);
    v = paramVal(params, "note");
    if (!v.empty()) noteMs = clampU(strtoul(v.c_str(), nullptr, 10), 80, 800);
    v = paramVal(params, "octave");
    if (!v.empty()) octave = (v == "low") ? LOW : (v == "high") ? HIGH : MID;
    v = paramVal(params, "mode");
    if (!v.empty()) autoPlay = (v == "auto");
  }

  void begin() override {
    hal::pinMode(greenPin, true);
    hal::pinMode(yellowPin, true);
    hal::pinMode(redPin, true);
    // The LEDC PWM (tone) owns the buzzer pin (see the board overlay) — don't pinMode
    // it; just silence it (the channel can power up at a default duty).
    hal::toneOff(buzzerPin);
    hal::pinMode(buttonPin, false);  // INPUT_PULLUP
    idx = 0;
    playing = false;
    noteOffAt = 0;
    lastBtn = 1;
    allLedsOff();
    nextAt = hal::nowMs();  // auto: play the first note at once
  }

  void tick() override {
    const uint32_t now = hal::nowMs();
    // Note-off: silence the current note after its length (shared by both modes).
    if (playing && now >= noteOffAt) silence();

    if (autoPlay) {
      if (!playing && now >= nextAt) {
        play(now);
        nextAt = noteOffAt + noteMs / 4;  // GAP_DIV — mirror the TS twin
      }
    } else {
      int btn = hal::pinRead(buttonPin);  // 0 = pressed (active-low)
      if (lastBtn == 1 && btn == 0) play(now);  // rising edge -> next note
      lastBtn = btn;
    }
  }

  bool done() override { return false; }  // continuous

 private:
  enum Octave { LOW, MID, HIGH };

  // 13-entry chromatic table (the C5 row, integer Hz). The octave scales it by
  // integer math so the firmware and the TS twin produce the EXACT same Hz.
  static int chromaticHz(int semitone) {
    static const int T[13] = {523, 554, 587, 622, 659, 698, 740,
                              784, 831, 880, 932, 988, 1047};
    if (semitone < 0 || semitone > 12) semitone = 0;
    return T[semitone];
  }
  int noteHz(int semitone) const {
    int base = chromaticHz(semitone);
    if (octave == LOW) return base / 2;
    if (octave == HIGH) return base * 2;
    return base;
  }

  // Each scale is a fixed list of semitone indices into the chromatic table.
  void setScale(const std::string& name) {
    static const int MAJ[8] = {0, 2, 4, 5, 7, 9, 11, 12};
    static const int MIN[8] = {0, 2, 3, 5, 7, 8, 10, 12};
    static const int BLU[7] = {0, 3, 5, 6, 7, 10, 12};
    static const int CHR[13] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    if (name == "minor")
      copyScale(MIN, 8);
    else if (name == "blues")
      copyScale(BLU, 7);
    else if (name == "chromatic")
      copyScale(CHR, 13);
    else
      copyScale(MAJ, 8);
  }
  void copyScale(const int* src, int n) {
    scaleLen = n;
    for (int i = 0; i < n; i++) scale[i] = src[i];
  }

  void play(uint32_t now) {
    hal::tone(buzzerPin, noteHz(scale[idx]));
    lightLed(idx % 3);  // the three LEDs mirror the current note
    playing = true;
    noteOffAt = now + noteMs;
    idx = (idx + 1) % scaleLen;
  }
  void silence() {
    hal::toneOff(buzzerPin);
    allLedsOff();
    playing = false;
  }
  void lightLed(int which) {
    hal::pinWrite(greenPin, which == 0 ? 1 : 0);
    hal::pinWrite(yellowPin, which == 1 ? 1 : 0);
    hal::pinWrite(redPin, which == 2 ? 1 : 0);
  }
  void allLedsOff() {
    hal::pinWrite(greenPin, 0);
    hal::pinWrite(yellowPin, 0);
    hal::pinWrite(redPin, 0);
  }

  static uint32_t clampU(unsigned long v, uint32_t lo, uint32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return (uint32_t)v;
  }
  // Value of `key=` in a space-separated param string (token-anchored).
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

  int greenPin = 1, yellowPin = 2, redPin = 4, buzzerPin = 5, buttonPin = 6;
  int scale[13] = {0, 2, 4, 5, 7, 9, 11, 12};  // default: major
  int scaleLen = 8;
  Octave octave = MID;
  uint32_t noteMs = 300;
  bool autoPlay = false;

  int idx = 0;
  bool playing = false;
  uint32_t noteOffAt = 0;
  uint32_t nextAt = 0;
  int lastBtn = 1;  // active-low: 1 = released
};
