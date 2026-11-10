#ifndef WEB_TELEMETRY_H
#define WEB_TELEMETRY_H

#include <string>

struct AppData;

// Whether the phone's next and prev buttons are live -- the same test as the
// box's (nextAvailable / prevAvailable).
struct NextPrevState {
    bool next_enabled = false;
    bool prev_enabled = false;
    bool sector_finish = false;   // next now ends an italian sector: the phone asks first
};

NextPrevState computeNextPrevState(AppData* data);

std::string buildTelemetryJson(AppData* data);
std::string buildStateJson(AppData* data);

#endif // WEB_TELEMETRY_H
