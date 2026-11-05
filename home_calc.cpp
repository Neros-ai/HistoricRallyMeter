#include "home_calc.h"
#include "calculations.h"
#include <cmath>
#include <cstdio>

std::vector<long> homeTripHistory(const RallyState& s) {
    std::vector<long> h = parseTripHistory(s.trip_history_m);
    if (h.size() > HOME_HISTORY_ENTRIES) h.resize(HOME_HISTORY_ENTRIES);
    return h;
}

double nextChangeSeconds(const std::vector<Segment>& segs, long cur, double remaining_m, bool* valid) {
    *valid = false;
    if (cur < 0 || cur >= static_cast<long>(segs.size())) return 0.0;
    const double ms = segs[cur].target_speed_kph / 3.6;
    if (ms <= 0.0) return 0.0;
    // Past the end of the row (a carried row the box does not step into):
    // the overrun counts against the rows that follow, so clamp the sum, not the remainder.
    double metres = remaining_m;
    for (long i = cur + 1; i < static_cast<long>(segs.size()); i++) {
        if (segs[i].changesSpeed) { *valid = true; return (metres > 0.0 ? metres : 0.0) / ms; }
        metres += segs[i].distance_m;
    }
    return 0.0;   // no later change: the stage just ends
}

long nextChangeIndex(const std::vector<Segment>& segs, long cur) {
    if (cur < 0) return -1;
    for (long i = cur + 1; i < static_cast<long>(segs.size()); i++)
        if (segs[i].changesSpeed) return i;
    return -1;
}

long metresToNextChange(const std::vector<Segment>& segs, long cur, long remaining_m) {
    if (cur < 0 || cur >= static_cast<long>(segs.size())) return remaining_m;
    double m = remaining_m;
    for (long i = cur + 1; i < static_cast<long>(segs.size()) && !segs[i].changesSpeed; i++)
        m += segs[i].distance_m;
    return std::lround(m);
}

long countdownMetres(const std::vector<Segment>& segs, long cur, long total_shown_m) {
    if (cur < 0 || cur >= static_cast<long>(segs.size())) return 0;
    const long next = nextChangeIndex(segs, cur);
    const long end_idx = next < 0 ? static_cast<long>(segs.size()) - 1 : next - 1;
    return static_cast<long>(stageRowEndMm(segs, end_idx) / 1000) - total_shown_m;
}

long rowCountdownMetres(const std::vector<Segment>& segs, long cur, int64_t stage_mm) {
    if (cur < 0 || cur >= static_cast<long>(segs.size())) return 0;
    const int64_t left_mm = stageRowEndMm(segs, cur) - stage_mm;
    return static_cast<long>(left_mm >= 0 ? (left_mm + 999) / 1000 : -((-left_mm) / 1000));
}

long foldCarriedRows(std::vector<Segment>& segs, long cur) {
    const long target = nextChangeIndex(segs, cur);
    if (target < 0 || target <= cur + 1) return 0;
    for (long i = cur + 1; i < target; i++) {
        segs[cur].distance_m += segs[i].distance_m;
    }
    // The merged row ends where the last folded row did, so it keeps that row's auto setting
    // (as mergeSegmentBack does): a roadbook that said wait for next must not turn into auto.
    segs[cur].autoNext = segs[target - 1].autoNext;
    segs.erase(segs.begin() + (cur + 1), segs.begin() + target);
    return target - cur - 1;
}

std::string formatClockMmSs(double seconds) {
    long t = static_cast<long>(std::lround(seconds));
    if (t < 0) t = 0;
    char buf[24];
    snprintf(buf, sizeof(buf), "%02ld:%02ld", t / 60, t % 60);
    return buf;
}

std::string driverSpeedText(double speed, int rally_type) {
    char buf[32];
    snprintf(buf, sizeof(buf), rally_type == 2 ? "%.2f" : "%.1f", speed);
    return buf;
}

StartWord homeStartWord(bool armedPending, bool firedThisStage, bool startedByAuto) {
    if (armedPending) return StartWord::AUTO;
    if (firedThisStage && startedByAuto) return StartWord::AUTO;
    return StartWord::MANUAL;
}

double homeStartTimeFont(double wordWidth, double timeWidthAt26, double columnW) {
    for (int f = 26; f > 18; f--)
        if (4 + wordWidth + 12 + timeWidthAt26 * f / 26.0 <= columnW - 4) return f;
    return 18;
}

bool showAdjZeroKey(const RallyState& s, bool single_display) {
    return s.measure_ab_bc && s.rally_type != 2 && !single_display;   // greek and surrey
}
