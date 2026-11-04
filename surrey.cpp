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
    if (ms < 0) return "";
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

bool fitStageToStart(std::vector<Segment>& stage_segs, double first_from_m, double navi_at_press_m, long calibration) {
    if (stage_segs.empty() || calibration <= 0) return false;
    Segment& row = stage_segs[0];
    if (navi_at_press_m < first_from_m - SURREY_START_SLACK_M ||
        navi_at_press_m >= first_from_m + row.distance_m)
        return false;
    row.distance_m = (first_from_m + row.distance_m) - navi_at_press_m;
    row.distance_counts = (row.distance_m * 1e6) / calibration;
    return true;
}

void surreyStart(RallyState& s, const RallyState::MemorySlot& slot) {
    const double navi_m = s.sector_start_odometer_cm / 100.0;
    fitStageToStart(s.stage_segments, slot.first_from_m, navi_m, s.calibration);
    // The previous stage's time taken stays on Home until the next stage finishes (mockup S1).
    s.surrey_finish_at_ms = 0;
}

bool surreyFinishAvailable(const RallyState& s) {
    return isSurrey(s) && !s.stage_complete && s.surrey_finish_at_ms == 0 &&
           s.segment_current_number >= 0 &&
           s.segment_current_number == static_cast<long>(s.stage_segments.size()) - 1;
}

bool surreyUndoAvailable(const RallyState& s, double navi_m, double last_to_m) {
    // The finished stage must still be the one on screen: a recalled slot, an edited roadbook or an
    // abort (segment -1) leaves the old figure behind and must not be reopened.
    return isSurrey(s) && s.stage_complete && s.surrey_finish_at_ms != 0 && navi_m < last_to_m &&
           s.segment_current_number >= 0 &&
           s.segment_current_number == static_cast<long>(s.stage_segments.size()) - 1 &&
           s.time_taken_stage == s.last_memory_slot;
}

std::string surreyLogTimeTaken(const RallyState& s) {
    if (!isSurrey(s) || !s.stage_complete || s.segment_current_number < 0 ||
        s.time_taken_stage != s.last_memory_slot)
        return "";
    return surreyTimeTakenText(s.time_taken_ms);
}

bool surreyUndoAvailableNow(const RallyState& s, uint64_t cntr1, uint64_t cntr2) {
    const int n = s.last_memory_slot;
    if (!isSurrey(s) || n < 1 || n > RallyState::MAX_MEMORY_SLOTS) return false;
    return surreyUndoAvailable(s, static_cast<double>(surreyNaviMetres(s, cntr1, cntr2)),
                               surreyLastToMetres(s.memory_slots[n - 1]));
}

bool applySurreyFinish(RallyState& s, int64_t now_ms) {
    if (!surreyFinishAvailable(s)) return false;
    s.time_taken_ms = now_ms - s.total_start_time_ms;
    s.time_taken_stage = s.last_memory_slot;
    s.surrey_finish_at_ms = now_ms;
    s.stage_complete = true;
    return true;
}

bool undoSurreyFinish(RallyState& s, double navi_m, double last_to_m) {
    if (!surreyUndoAvailable(s, navi_m, last_to_m)) return false;
    s.stage_complete = false;
    s.time_taken_ms = -1;
    s.time_taken_stage = 0;
    s.surrey_finish_at_ms = 0;
    return true;
}

void surreyDistanceEnd(RallyState& s, int64_t now_ms) {
    // surrey_finish_at_ms != 0: a next press already froze the figure.
    if (!isSurrey(s) || s.surrey_finish_at_ms != 0) return;
    s.time_taken_ms = now_ms - s.total_start_time_ms;
    s.time_taken_stage = s.last_memory_slot;
}

double surreyLastToMetres(const RallyState::MemorySlot& slot) {
    double to = slot.first_from_m;
    for (const Segment& g : slot.segments) to += g.distance_m;
    return to;
}

double beepDistanceM(const RallyState& s, uint64_t c1, uint64_t c2, double stage_travelled_m) {
    if (!isSurrey(s)) return stage_travelled_m;
    double from = 0.0;
    if (s.last_memory_slot >= 1 && s.last_memory_slot <= RallyState::MAX_MEMORY_SLOTS)
        from = s.memory_slots[s.last_memory_slot - 1].first_from_m;
    return static_cast<double>(surreyNaviMetres(s, c1, c2)) - from;
}
