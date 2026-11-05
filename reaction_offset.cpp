#include "reaction_offset.h"
#include "sector.h"
#include <algorithm>
#include <cmath>

int clampReactionOffsetMs(long ms) {
    return static_cast<int>(std::max<long>(-REACTION_OFFSET_MAX_MS, std::min<long>(REACTION_OFFSET_MAX_MS, ms)));
}

int64_t pressTimeMs(int64_t press_ms, int offset_ms) { return press_ms - offset_ms; }

double pressBackMetres(double speed_mps, int offset_ms) {
    if (speed_mps < 0.0) speed_mps = 0.0;
    return speed_mps * offset_ms / 1000.0;
}

int64_t pressBackMm(double speed_mps, int offset_ms) {
    return static_cast<int64_t>(std::llround(pressBackMetres(speed_mps, offset_ms) * 1000.0));
}

double reactionSpeedMps(double displayed_speed, bool mph) {
    if (displayed_speed < 0.0) return 0.0;
    return displayed_speed * (mph ? 1.609344 : 1.0) / 3.6;
}

void applyStartPressOffset(RallyState& s, double speed_mps) {
    const int off = s.reaction_offset_ms;
    if (off == 0 || speed_mps * 3.6 < REACTION_FLYING_MIN_KPH - 1e-9) return;   // standing start: untouched
    if (s.rally_type == 2) {   // distance is pinned to the card; only the tube clock moves
        if (stageIsSector(s.stage_segments) && s.trial_start_m == 0.0)
            s.trial_start_ms = pressTimeMs(s.trial_start_ms, off);
        return;
    }
    s.total_start_time_ms = pressTimeMs(s.total_start_time_ms, off);
    s.trip_start_time_ms = pressTimeMs(s.trip_start_time_ms, off);
    s.segment_start_time_ms = pressTimeMs(s.segment_start_time_ms, off);
    const long back = static_cast<long>(pressBackMm(speed_mps, off));
    s.total_distance_adjust_mm += back;
    s.trip_distance_adjust_mm += back;   // Trip and Total agree at a flying start
    if (s.rally_type == 1) s.sector_start_odometer_mm -= back;   // surrey: navi total at the real point
}

void applyNextBaselineOffset(RallyState& s, double speed_mps) {
    if (s.reaction_offset_ms == 0) return;
    const long back = static_cast<long>(pressBackMm(speed_mps, s.reaction_offset_ms));
    s.segment_start_adjust_mm -= back;
    s.trip_distance_adjust_mm += back;
}

int64_t boundaryMmAtPress(int64_t driven_mm, double speed_mps, int offset_ms) {
    const int64_t back = pressBackMm(speed_mps, offset_ms);
    if (back == 0) return driven_mm;   // offset zero (or standing still): exactly today's figure
    return std::max<int64_t>(0, driven_mm - back);
}
