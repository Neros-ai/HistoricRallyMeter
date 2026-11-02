#include "surrey.h"
#include "calculations.h"
#include "home_calc.h"
#include "sector.h"
#include <cstdio>

bool isSurrey(const RallyState& s) {
    return s.rally_type == 1;
}

long surreyNaviMetres(const RallyState& s, uint64_t c1, uint64_t c2) {
    return adjustedDistanceMeters(countsToCentimeters(odometerCounts(s, c1, c2), s.calibration), 0);
}

int tripHistoryRows(const RallyState&) {
    return static_cast<int>(HOME_HISTORY_ENTRIES);
}

std::string surreyTimeTakenText(int64_t ms) {
    if (ms < 0) ms = 0;
    const int64_t tenths = (ms + 50) / 100;
    const int64_t secs = tenths / 10;
    char buf[32];
    if (secs >= 3600)
        snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld.%lld", static_cast<long long>(secs / 3600),
                 static_cast<long long>((secs / 60) % 60), static_cast<long long>(secs % 60),
                 static_cast<long long>(tenths % 10));
    else
        snprintf(buf, sizeof(buf), "%lld:%02lld.%lld", static_cast<long long>(secs / 60),
                 static_cast<long long>(secs % 60), static_cast<long long>(tenths % 10));
    return buf;
}

std::string surreyCountdownTime(double metres, double kph) {
    if (kph <= 0.0) return "";
    return formatClockMmSs((metres > 0.0 ? metres : 0.0) / (kph / 3.6));
}
