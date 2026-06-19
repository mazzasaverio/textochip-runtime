#pragma once
#include <string>

// A MISSION is a complete behavior, tick-based (non-blocking) — brief §8.
// Identical interface to the Arduino firmware.
class Mission {
 public:
  virtual void begin() = 0;  // setup / start
  virtual void tick() = 0;   // advance one step, never blocks
  virtual bool done() = 0;   // true when finished (false = continuous)
  virtual ~Mission() {}
};

// string id (+ pins baked into the bytecode) -> native mission object, or nullptr.
Mission* missionFor(const std::string& id, const int* args, int argc);
