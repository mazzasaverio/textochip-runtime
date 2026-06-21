#include <string>

#include "mission.h"
#include "pianola.h"
#include "semaforo.h"

// Native mission registry: string id -> a configured Mission*. Add a mission =
// include its header + one REGISTRY row below. Each instance is created lazily
// on first CALL (function-local static), so a large catalog of unused missions
// costs no RAM. Mirrors the simulator registry (lib/backends/missions.ts).
//
// Every native mission takes the same 5 board pins (green yellow red buzzer
// button) + a `key=value` param string, and owns its guardrails in setParams().
namespace {

template <typename T>
Mission* makeMission(const int* args, int argc, const std::string& params) {
  static T m;
  if (argc >= 5) m.configure(args[0], args[1], args[2], args[3], args[4]);
  m.setParams(params);
  return &m;
}

struct MissionEntry {
  const char* id;
  Mission* (*make)(const int*, int, const std::string&);
};

const MissionEntry REGISTRY[] = {
    {"SEMAFORO", makeMission<Semaforo>},
    {"PIANOLA", makeMission<Pianola>},
};

}  // namespace

Mission* missionFor(const std::string& id, const int* args, int argc,
                    const std::string& params) {
  for (const MissionEntry& e : REGISTRY) {
    if (id == e.id) return e.make(args, argc, params);
  }
  return nullptr;
}
