#include "mission.h"
#include "semaforo.h"

// string id -> native mission (CALL <name> dispatch).
Mission* missionFor(const std::string& id, const int* args, int argc,
                    const std::string& params) {
  if (id == "SEMAFORO") {
    static Semaforo s;
    // CALL SEMAFORO <green> <yellow> <red> <buzzer> <button> [key=value …]
    if (argc >= 5) s.configure(args[0], args[1], args[2], args[3], args[4]);
    s.setParams(params);  // durations / beep / button / minGreen (clamped)
    return &s;
  }
  return nullptr;
}
