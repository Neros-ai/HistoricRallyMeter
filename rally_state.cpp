#include "rally_state.h"
#include <chrono>

RallyState::RallyState() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    total_start_time_ms = ms;
    trip_start_time_ms = ms;
    segment_start_time_ms = ms;
}

bool greekBeepSettingsLocked(const RallyState& s) {
    return s.rally_type == 2;
}

void applyRallyTypeDefaults(RallyState& s, int type) {
    // The rally type changes the rules only. Distance units, kph / mph and the measure type are
    // the crew's own choices (Settings) and are left exactly as they are.
    if (type == 0 || type == 1 || type == 2) s.rally_type = type;
}
