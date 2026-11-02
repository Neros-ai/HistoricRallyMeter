#include "driver_trial.h"
#include "sector.h"
#include <algorithm>
#include <cstdio>

double finalWindowProgress(double to_line_s) {
    if (to_line_s >= DRIVER_FINAL_S) return 0.0;
    if (to_line_s <= 0.0) return 1.0;
    return (DRIVER_FINAL_S - to_line_s) / DRIVER_FINAL_S;
}

std::string countdownBoxText(double to_line_s) {
    const double v = std::clamp(to_line_s, 0.0, DRIVER_FINAL_S);
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f", v);
    return buf;
}

std::string trialSpeedText(double kph) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", kph);
    return buf;
}

std::string trialKmText(double metres) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", metres / 1000.0);
    return buf;
}

DriverTrialView driverTrialView(const RallyState& s, uint64_t c1, uint64_t c2, int64_t now_ms) {
    DriverTrialView v;
    const long cur = s.segment_current_number;
    if (s.rally_type != 2 || s.stage_complete || !stageIsSector(s.stage_segments)) return v;
    if (cur < 0 || cur >= static_cast<long>(s.stage_segments.size())) return v;
    const Segment& row = s.stage_segments[static_cast<size_t>(cur)];
    if (row.type != SEG_PC && row.type != SEG_PM) return v;
    if (s.trial_start_m < 0.0) return v;   // no tube clock (e.g. after a power loss)
    bool valid = false;
    const double to_line = sectorSecondsToLine(s, now_ms, &valid);
    if (!valid) return v;

    v.active = true;
    v.tag = sectorTag(row);
    v.isPc = row.type == SEG_PC;
    v.sectorTotalM = sectorPositionM(s, c1, c2);
    const double into_m = v.sectorTotalM - s.trial_start_m;
    const double secs = (now_ms - s.trial_start_ms) / 1000.0;
    if (secs > 0.5 && into_m > 0.0) {
        v.avgKph = into_m / secs * 3.6;
        v.avgValid = true;
    }
    v.toLineS = to_line;
    v.durationS = to_line + secs;
    v.boxS = std::clamp(to_line, 0.0, DRIVER_FINAL_S);
    v.boxLive = to_line < DRIVER_FINAL_S;
    v.arrowProgress = finalWindowProgress(to_line);
    return v;
}
