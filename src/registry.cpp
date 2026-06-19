#include "mission.h"
#include "semaforo.h"

// string id -> native mission (mirrors the Arduino missions/registry.h).
Mission* missionFor(const std::string& id, const int* args, int argc) {
  if (id == "SEMAFORO") {
    static Semaforo s;
    // CALL SEMAFORO <green> <yellow> <red> <buzzer> <button>
    if (argc >= 5) s.configure(args[0], args[1], args[2], args[3], args[4]);
    return &s;
  }
  return nullptr;
}
