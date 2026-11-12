#include "surrey.h"
#include "calculations.h"
#include "home_calc.h"
#include "sector.h"
#include <cstdio>

bool isSurrey(const RallyState& s) {
    return s.rally_type == 1;
}

long surreyNaviMetres(const RallyState& s, uint64_t c1, uint64_t c2) {
    return adjustedDistanceMetersMm(odometerMm(s, c1, c2), 0);
}

long metresToStageStart(const RallyState& s, uint64_t c1, uint64_t c2, long start_distance_m) {
    if (start_distance_m <= 0) return 0;   // no "Distance at start" entered: nothing to arrive at, even with Total negative
    return start_distance_m - (isSurrey(s) ? surreyNaviMetres(s, c1, c2) : totalShownMetres(s, c1, c2));
}

const char* const SURREY_TOTAL_RESET_NOTICE = "Use set during a stage";

bool surreyStageLive(const RallyState& s) {
    if (!isSurrey(s)) return false;
    const bool running = s.segment_current_number >= 0 && !s.stage_complete;
    const bool early_countdown = s.auto_start_rally_time_s != 0 && s.auto_start_early_departure;
    return running || early_countdown;
}

bool surreySetTotal(RallyState& s, uint64_t c1, uint64_t c2, double metres) {
    if (!isSurrey(s) || metres < 0.0) return false;
    const int64_t before_mm = odometerMm(s, c1, c2);
    if (!setOdometerMetres(s, c1, c2, metres)) return false;
    if (surreyStageLive(s))
        s.total_distance_adjust_mm += static_cast<long>(odometerMm(s, c1, c2) - before_mm);
    return true;
}

bool surreyTotalResetBlocked(const RallyState& s) {
    return surreyStageLive(s);
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
    return true;
}

void surreyStart(RallyState& s, const RallyState::MemorySlot& slot) {
    const double navi_m = s.sector_start_odometer_mm / 1000.0;
    fitStageToStart(s.stage_segments, slot.first_from_m, navi_m, s.calibration);
    // The previous stage's time taken stays on Home until the next stage finishes (mockup S1).
    // It is remembered here so prev can put it back after a finish pressed by mistake. On an
    // early departure this runs at the arm and again at the minute; the second time the figure
    // on Home is still the previous stage's, so remembering it again changes nothing.
    s.prev_time_taken_ms = s.time_taken_ms;
    s.prev_time_taken_stage = s.time_taken_stage;
    s.surrey_finish_at_ms = 0;
    s.surrey_distance_end_ms = -1;
}

bool surreyFinishAvailable(const RallyState& s) {
    // The roadbook's distance ran out before the crew reached the real finish: the stage has
    // ended on distance (surreyDistanceEnd took a time) but no finish press has been made. The
    // press is still taken, once, and its time replaces the distance one.
    const bool ended_on_distance = s.stage_complete && s.time_taken_ms >= 0 &&
                                   s.surrey_distance_end_ms >= 0 &&
                                   s.time_taken_stage == s.last_memory_slot;
    return isSurrey(s) && (!s.stage_complete || ended_on_distance) && s.surrey_finish_at_ms == 0 &&
           s.segment_current_number >= 0 &&
           s.segment_current_number == static_cast<long>(s.stage_segments.size()) - 1;
}

bool surreyUndoAvailable(const RallyState& s, double navi_m, double last_to_m) {
    // The finished stage must still be the one on screen: a recalled slot, an edited roadbook or an
    // abort (segment -1) leaves the old figure behind and must not be reopened.
    // A finish pressed after the stage had ended on distance can be taken back anywhere (the
    // distance-end time returns). One pressed while the stage ran can be taken back only short of
    // the roadbook's end: past it the stage cannot be reopened, its distance has run out.
    const bool late_press = s.surrey_distance_end_ms >= 0;
    return isSurrey(s) && s.stage_complete && s.surrey_finish_at_ms != 0 &&
           (late_press || navi_m < last_to_m) &&
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
    s.surrey_finish_at_ms = 0;
    if (s.surrey_distance_end_ms >= 0) {
        // A late press taken back: the stage stays ended, on the time its distance gave it, and
        // the finish can be pressed again.
        s.time_taken_ms = s.surrey_distance_end_ms;
        return true;
    }
    // A press while the stage ran, taken back: the stage carries on and Home shows what it
    // showed before the press -- the previous stage's time taken.
    s.stage_complete = false;
    s.time_taken_ms = s.prev_time_taken_ms;
    s.time_taken_stage = s.prev_time_taken_stage;
    return true;
}

void surreyDistanceEnd(RallyState& s, int64_t now_ms) {
    // surrey_finish_at_ms != 0: a next press already froze the figure.
    if (!isSurrey(s) || s.surrey_finish_at_ms != 0) return;
    s.time_taken_ms = now_ms - s.total_start_time_ms;
    s.time_taken_stage = s.last_memory_slot;
    s.surrey_distance_end_ms = s.time_taken_ms;   // kept: prev takes a late finish press back to it
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
