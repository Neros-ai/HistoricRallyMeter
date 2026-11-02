#include "sector.h"
#include "calculations.h"
#include "stage_rows.h"
#include <algorithm>
#include <cmath>

std::string typeLabel(int type) {
    switch (type) {
    case SEG_NAVI: return "navi";
    case SEG_PC:   return "pc";
    case SEG_CT:   return "ct";
    case SEG_PM:   return "pm";
    case SEG_CO:   return "co";
    default:       return "";
    }
}

std::string sectorTag(const Segment& s) {
    const std::string label = typeLabel(s.type);
    if (label.empty() || s.type == SEG_NAVI) return label;
    return label + std::to_string(s.ref);
}

int stepSegmentType(int type) {
    switch (type) {
    case SEG_NAVI: return SEG_PC;
    case SEG_PC:   return SEG_CT;
    case SEG_CT:   return SEG_PM;
    case SEG_PM:   return SEG_CO;
    default:       return SEG_NAVI;
    }
}

int nextRef(const std::vector<Segment>& segs, int type) {
    int last = 0;
    for (const Segment& s : segs) if (s.type == type && s.ref > last) last = s.ref;
    return last + 1;
}

bool stageIsSector(const std::vector<Segment>& segs) {
    for (const Segment& s : segs) if (s.type != SEG_LEG) return true;
    return false;
}

bool rowStartsTube(const std::vector<Segment>& segs, long i) {
    if (i < 0 || i >= static_cast<long>(segs.size())) return false;
    if (segs[i].type == SEG_PC) return true;
    if (segs[i].type != SEG_PM) return false;
    return !(i > 0 && segs[i - 1].type == SEG_PM && segs[i - 1].ref == segs[i].ref);
}

double rowSeconds(const Segment& s) {
    return s.target_speed_kph > 0.0 ? s.distance_m / (s.target_speed_kph / 3.6) : 0.0;
}

void applySectorRules(std::vector<Segment>& segs, long sector_duration_s) {
    double timed_s = 0.0, untimed_m = 0.0;
    for (Segment& s : segs) {
        if (s.type == SEG_LEG) continue;
        s.changesSpeed = true;    // each row has its own speed: no carrying the one above
        s.autoNext = false;       // every boundary waits for next
        if (s.type == SEG_PC && s.pc_duration_s > 0.0)
            s.target_speed_kph = s.distance_m / s.pc_duration_s * 3.6;
        if (s.type == SEG_PC || s.type == SEG_PM) timed_s += rowSeconds(s);
        else untimed_m += s.distance_m;
    }
    if (sector_duration_s <= 0 || untimed_m <= 0.0 || sector_duration_s <= timed_s) return;
    const double kph = untimed_m / 1000.0 / ((sector_duration_s - timed_s) / 3600.0);
    for (Segment& s : segs)
        if (s.type == SEG_NAVI || s.type == SEG_CT || s.type == SEG_CO) s.target_speed_kph = kph;
}

std::string insertSectorDistances(std::vector<Segment>& segs, double first_from_m,
                                  const std::vector<double>& to_m) {
    // A distance inside a pc or pm splits it like any other row (owner, 2026-10-31), so a
    // wrongly entered trial can be corrected. Note which trials are about to be split.
    std::vector<std::pair<int, int>> split;   // (type, ref) of each pc / pm a distance falls inside
    for (double to : to_m) {
        double from = first_from_m;
        for (const Segment& s : segs) {
            const double end = from + s.distance_m;
            if (to > from && to < end && (s.type == SEG_PC || s.type == SEG_PM)) split.push_back({s.type, s.ref});
            from = end;
        }
    }
    std::string notice = insertStageDistances(segs, first_from_m, to_m);
    for (size_t i = 0; i < segs.size(); i++) {
        if (segs[i].type != SEG_LEG) continue;
        segs[i].type = i > 0 ? segs[i - 1].type : SEG_NAVI;
        std::vector<Segment> above(segs.begin(), segs.begin() + static_cast<long>(i));
        segs[i].ref = (segs[i].type == SEG_NAVI) ? 0 : nextRef(above, segs[i].type);
        segs[i].changesSpeed = true;
        segs[i].autoNext = false;
    }
    // A split leaves two rows of the same trial side by side. A pm simply continues over both
    // (same ref, same speed). A pc becomes two pcs: the card time is shared by distance so the
    // speed is unchanged, the second half takes the next number and later pcs move up one.
    for (const auto& t : split) {
        for (size_t i = 0; i + 1 < segs.size(); i++) {
            Segment& a = segs[i];
            Segment& b = segs[i + 1];
            if (a.type != t.first || b.type != t.first || a.ref != t.second || b.ref != t.second) continue;
            a.autoNext = b.autoNext = false;
            b.changesSpeed = true;
            if (t.first == SEG_PC) {
                const double total = a.pc_duration_s;
                const double len = a.distance_m + b.distance_m;
                a.pc_duration_s = len > 0 ? static_cast<double>(std::lround(total * a.distance_m / len)) : 0.0;
                b.pc_duration_s = total - a.pc_duration_s;
                for (size_t k = i + 1; k < segs.size(); k++)
                    if (segs[k].type == SEG_PC) segs[k].ref++;
            }
            break;
        }
    }
    return notice;
}

long parseDurationSeconds(const std::string& text) {
    long parts[3] = {0, 0, 0};
    int n = 0;
    long cur = 0;
    bool digits = false;
    for (char c : text) {
        if (c >= '0' && c <= '9') { cur = cur * 10 + (c - '0'); digits = true; }
        else if (c == ':' && digits && n < 2) { parts[n++] = cur; cur = 0; digits = false; }
        else if (c == ' ') continue;
        else return -1;
    }
    if (!digits) return -1;
    parts[n++] = cur;
    long total = 0;
    for (int i = 0; i < n; i++) total = total * 60 + parts[i];
    return total;
}

int64_t italianStageStartMs(int64_t press_ms) {
    return press_ms - (press_ms % 60000);
}

void markSectorStart(RallyState& s) {
    if (s.rally_type != 2) return;
    if (!stageIsSector(s.segments) && !stageIsSector(s.stage_segments)) return;
    s.sector_start_ms = italianStageStartMs(s.total_start_time_ms);
    s.total_start_time_ms = sectorClockZeroMs(s);   // the start time: 30 s through the minute
    s.sector_undo = RallyState::SectorUndo();   // nothing from an earlier sector to undo
    double card_s = 0.0;
    for (const Segment& g : s.stage_segments) card_s += rowSeconds(g);
    s.sector_card_ms = static_cast<int64_t>(std::llround(card_s * 1000.0));
    if (rowStartsTube(s.stage_segments, 0)) {
        s.trial_start_ms = s.segment_start_time_ms;   // the real press, not the minute
        s.trial_start_m = 0.0;
    } else {
        s.trial_start_m = -1.0;
    }
}

int64_t sectorClockZeroMs(const RallyState& s) {
    return s.sector_start_ms + SECTOR_AIM_MS;
}

int64_t coInTimeMs(const RallyState& s) {
    return s.sector_start_ms + s.sector_card_ms;
}

namespace {

double endOfRowM(const std::vector<Segment>& segs, long i) {
    double m = 0.0;
    for (long k = 0; k <= i && k < static_cast<long>(segs.size()); k++) m += segs[k].distance_m;
    return m;
}

}  // namespace

bool applySectorNext(RallyState& s, uint64_t c1, uint64_t c2, int64_t now_ms) {
    const std::vector<Segment>& segs = s.stage_segments;
    const long cur = s.segment_current_number;
    if (!stageIsSector(segs) || cur < 0 || cur + 1 >= static_cast<long>(segs.size())) return false;

    const int64_t raw_counts = calculateDistanceCounts(s, c1, c2, s.total_start_cntr1, s.total_start_cntr2);
    const long raw_cm = countsToCentimeters(raw_counts, s.calibration);
    const double here_m = (raw_cm + s.total_distance_adjust_cm) / 100.0;
    const double end_m = endOfRowM(segs, cur);

    RallyState::SectorUndo& u = s.sector_undo;
    u.valid = true;
    u.index = cur;
    u.total_start_ms = s.total_start_time_ms;
    u.total_adjust_cm = s.total_distance_adjust_cm;
    u.seg_c1 = s.segment_start_cntr1; u.seg_c2 = s.segment_start_cntr2;
    u.seg_adjust_cm = s.segment_start_adjust_cm;
    u.seg_start_ms = s.segment_start_time_ms;
    u.trip_c1 = s.trip_start_cntr1; u.trip_c2 = s.trip_start_cntr2;
    u.trip_start_ms = s.trip_start_time_ms;
    u.trip_adjust_cm = s.trip_distance_adjust_cm;
    u.trip_history = s.trip_history_m;
    u.pc_error = s.last_pc_error;
    u.trial_start_ms = s.trial_start_ms;
    u.trial_start_m = s.trial_start_m;
    s.undo_valid = false;             // today's prev is for ordinary next presses

    // The pc just finished: its error against the card, timed from its own tube.
    if (segs[cur].type == SEG_PC) {
        RallyState::PcError err;
        err.valid = true;
        err.ref = segs[cur].ref;
        err.seconds = (now_ms - s.trial_start_ms) / 1000.0 - segs[cur].pc_duration_s;
        err.metres = static_cast<long>(std::lround(here_m - end_m));
        s.last_pc_error = err;
    }

    // Every line is a printed card point: Total is pinned there as `set` pins it. No clock
    // moves and no row length changes, so the schedule and the co in-time stay the card's.
    s.total_distance_adjust_cm = static_cast<long>(std::lround(end_m * 100.0)) - raw_cm;

    const Segment& next = segs[cur + 1];
    if (rowStartsTube(segs, cur + 1)) {
        s.trial_start_ms = now_ms;
        s.trial_start_m = end_m;
    } else if (!(next.type == SEG_PM && segs[cur].type == SEG_PM && next.ref == segs[cur].ref)) {
        s.trial_start_m = -1.0;   // a pm continuing the same ref keeps its clock
    }

    s.segment_current_number = cur + 1;
    s.segment_start_cntr1 = c1;
    s.segment_start_cntr2 = c2;
    s.segment_start_adjust_cm = s.total_distance_adjust_cm;
    s.segment_start_time_ms = now_ms;
    rebaseTripToSegment(s, c1, c2);
    u.after_total_adjust_cm = s.total_distance_adjust_cm;
    u.after_trip_adjust_cm = s.trip_distance_adjust_cm;
    return true;
}

bool undoSectorTube(RallyState& s) {
    RallyState::SectorUndo& u = s.sector_undo;
    if (!sectorUndoAvailable(s)) return false;
    // A -10 or set made after the next is kept: only what the next itself changed goes back.
    const long total_since_cm = s.total_distance_adjust_cm - u.after_total_adjust_cm;
    const long trip_since_cm = s.trip_distance_adjust_cm - u.after_trip_adjust_cm;
    s.segment_current_number = u.index;
    s.total_start_time_ms = u.total_start_ms;
    s.total_distance_adjust_cm = u.total_adjust_cm + total_since_cm;
    s.segment_start_cntr1 = u.seg_c1; s.segment_start_cntr2 = u.seg_c2;
    s.segment_start_adjust_cm = u.seg_adjust_cm;
    s.segment_start_time_ms = u.seg_start_ms;
    s.trip_start_cntr1 = u.trip_c1; s.trip_start_cntr2 = u.trip_c2;
    s.trip_start_time_ms = u.trip_start_ms;
    s.trip_distance_adjust_cm = u.trip_adjust_cm + trip_since_cm;
    s.trip_history_m = u.trip_history;
    s.last_pc_error = u.pc_error;
    s.trial_start_ms = u.trial_start_ms;
    s.trial_start_m = u.trial_start_m;
    u = RallyState::SectorUndo();
    return true;
}

namespace {

bool inTrial(const RallyState& s) {
    return s.trial_start_m >= 0.0;
}

// Schedule seconds from the trial's start tube to `m`; < 0 when the roadbook has a hole.
double trialIdealSeconds(const RallyState& s, double m) {
    const double to = idealSecondsToReachDistance(s.stage_segments, m);
    const double from = s.trial_start_m > 0.0 ? idealSecondsToReachDistance(s.stage_segments, s.trial_start_m) : 0.0;
    return (to < 0.0 || from < 0.0) ? -1.0 : to - from;
}

}  // namespace

double sectorSecondsToLine(const RallyState& s, int64_t now_ms, bool* valid) {
    *valid = false;
    const long cur = s.segment_current_number;
    if (cur < 0 || cur >= static_cast<long>(s.stage_segments.size())) return 0.0;
    const double end_m = endOfRowM(s.stage_segments, cur);
    if (inTrial(s)) {
        const double ideal_s = trialIdealSeconds(s, end_m);
        if (ideal_s < 0.0) return 0.0;
        *valid = true;
        return ideal_s - (now_ms - s.trial_start_ms) / 1000.0;
    }
    const double ideal_s = idealSecondsToReachDistance(s.stage_segments, end_m);
    if (ideal_s < 0.0) return 0.0;
    *valid = true;
    return ideal_s - (now_ms - sectorClockZeroMs(s)) / 1000.0;
}

double pcCountdownSeconds(const RallyState& s, int64_t now_ms, bool* valid) {
    *valid = false;
    const long cur = s.segment_current_number;
    if (s.stage_complete || cur < 0 || cur >= static_cast<long>(s.stage_segments.size())) return 0.0;
    if (s.stage_segments[static_cast<size_t>(cur)].type != SEG_PC || !inTrial(s)) return 0.0;
    return sectorSecondsToLine(s, now_ms, valid);
}

PcBeep pcCountdownBeep(double prev_s, double now_s) {
    if (prev_s > 0.0 && now_s <= 0.0) return PcBeep::Zero;
    static const double marks[] = { 30, 15, 5, 4, 3, 2, 1 };
    for (double m : marks)
        if (prev_s > m && now_s <= m) return PcBeep::Tick;
    return PcBeep::None;
}

bool sectorUndoAvailable(const RallyState& s) {
    return stageIsSector(s.stage_segments) && s.sector_undo.valid
        && s.segment_current_number == s.sector_undo.index + 1;
}

int64_t odometerCounts(const RallyState& s, uint64_t c1, uint64_t c2) {
    const int64_t raw = calculateDistanceCounts(s, c1, c2, s.odometer_start_cntr1, s.odometer_start_cntr2);
    return correctedDistanceCounts(raw, s.odometer_adjust_cm, s.calibration);
}

bool setOdometerMetres(RallyState& s, uint64_t c1, uint64_t c2, double metres) {
    if (metres < 0.0) return false;
    const int64_t raw = calculateDistanceCounts(s, c1, c2, s.odometer_start_cntr1, s.odometer_start_cntr2);
    s.odometer_adjust_cm = static_cast<long>(std::lround(metres * 100.0)) - countsToCentimeters(raw, s.calibration);
    return true;
}

void markSectorOdometer(RallyState& s, uint64_t c1, uint64_t c2) {
    if (s.rally_type != 2) return;
    s.sector_start_odometer_cm = countsToCentimeters(odometerCounts(s, c1, c2), s.calibration);
}

void resetOdometerKeepingPosition(RallyState& s, uint64_t c1, uint64_t c2) {
    const long position_cm = countsToCentimeters(odometerCounts(s, c1, c2), s.calibration) - s.sector_start_odometer_cm;
    s.odometer_start_cntr1 = c1;
    s.odometer_start_cntr2 = c2;
    s.odometer_adjust_cm = 0;
    s.sector_start_odometer_cm = -position_cm;
}

double sectorPositionM(const RallyState& s, uint64_t c1, uint64_t c2) {
    const int64_t counts = correctedDistanceCounts(
        calculateDistanceCounts(s, c1, c2, s.total_start_cntr1, s.total_start_cntr2),
        s.total_distance_adjust_cm, s.calibration);
    return std::max<long>(0, countsToCentimeters(counts, s.calibration)) / 100.0;
}

double sectorAheadSeconds(const RallyState& s, uint64_t c1, uint64_t c2, int64_t now_ms, bool* valid) {
    *valid = false;
    if (!stageIsSector(s.stage_segments) || s.stage_complete || s.segment_current_number < 0) return 0.0;
    const double ideal_s = idealSecondsToReachDistance(s.stage_segments, sectorPositionM(s, c1, c2));
    if (ideal_s < 0.0) return 0.0;
    *valid = true;
    return ideal_s - (now_ms - sectorClockZeroMs(s)) / 1000.0;
}

double trialAheadSeconds(const RallyState& s, uint64_t c1, uint64_t c2, int64_t now_ms, bool* valid) {
    *valid = false;
    if (!stageIsSector(s.stage_segments) || s.stage_complete || s.segment_current_number < 0 || !inTrial(s))
        return 0.0;
    const double ideal_s = trialIdealSeconds(s, sectorPositionM(s, c1, c2));
    if (ideal_s < 0.0) return 0.0;
    *valid = true;
    return ideal_s - (now_ms - s.trial_start_ms) / 1000.0;
}

double italianAheadSeconds(const RallyState& s, uint64_t c1, uint64_t c2, int64_t now_ms, bool* valid) {
    return inTrial(s) ? trialAheadSeconds(s, c1, c2, now_ms, valid) : sectorAheadSeconds(s, c1, c2, now_ms, valid);
}

double italianNeedleSeconds(const RallyState& s, uint64_t c1, uint64_t c2, int64_t now_ms, bool* valid) {
    return trialAheadSeconds(s, c1, c2, now_ms, valid);
}

double gaugeAheadSeconds(const RallyState& s, uint64_t c1, uint64_t c2, int64_t now_ms,
                         int64_t corrected_stage_counts) {
    if (s.rally_type == 2 && stageIsSector(s.stage_segments)) {
        bool valid = false;
        const double secs = italianNeedleSeconds(s, c1, c2, now_ms, &valid);
        return valid ? secs : 0.0;
    }
    return calculateAheadBehindFromStageStart(s, now_ms, corrected_stage_counts)
         + s.ahead_behind_zero_offset_ms / 1000.0;
}

long minutesInHand(double ahead_s) {
    return std::min(99L, std::lround(std::fabs(ahead_s) / 60.0));
}

const char* readoutKeyName(int rally_type, int row) {
    // Rows 3 and 4 are the next and prev keys: greek / surrey step by speed, italian by substage.
    static const char* names[2][5] = { {"total", "trip", "countdown", "next\nspeed", "prev\nspeed"},
                                       {"sector\ntotal", "sub", "navi", "next\nsubstage", "prev\nsubstage"} };
    if (row < 0 || row > 4) return "";
    return names[rally_type == 2 ? 1 : 0][row];
}

bool sectorPreviewWanted(const RallyState& s) {
    return s.stage_complete && !s.segments.empty() && stageIsSector(s.segments);
}

RallyState sectorPreviewState(const RallyState& live, int64_t now_ms) {
    RallyState v(live);
    v.stage_segments = live.segments;
    v.segment_current_number = 0;
    v.stage_complete = false;
    v.segment_start_time_ms = now_ms;
    v.total_start_time_ms = now_ms;
    markSectorStart(v);   // arrival as if the sector started this minute
    return v;
}

void zeroOdometer(RallyState& s, uint64_t c1, uint64_t c2) {
    s.odometer_start_cntr1 = c1;
    s.odometer_start_cntr2 = c2;
    s.odometer_adjust_cm = 0;
}

SectorPanel sectorPanel(const RallyState& s, int64_t now_ms, long stage_m) {
    SectorPanel p;
    const std::vector<Segment>& segs = s.stage_segments;
    const long cur = s.segment_current_number;
    p.running = !s.stage_complete && cur >= 0 && cur < static_cast<long>(segs.size());
    if (!p.running) return p;
    const Segment& row = segs[cur];
    p.kind = row.type == SEG_PC ? SectorPanel::PC : row.type == SEG_PM ? SectorPanel::PM : SectorPanel::NAVI;
    p.inTag = sectorTag(row);
    p.inKph = row.target_speed_kph;
    if (cur + 1 < static_cast<long>(segs.size())) p.nextTag = sectorTag(segs[cur + 1]);
    p.nextAtM = static_cast<long>(std::lround(endOfRowM(segs, cur)));
    p.metresToGo = std::max<long>(0, p.nextAtM - stage_m);
    // navi / ct: no schedule time (crews drive them flat out); pc, pm and co keep their countdown.
    if (row.type != SEG_NAVI && row.type != SEG_CT)
        p.secondsToLine = sectorSecondsToLine(s, now_ms, &p.secondsValid);
    if (p.kind == SectorPanel::PC && s.trial_start_m >= 0.0) {
        const double into_m = stage_m - s.trial_start_m;
        const double secs = (now_ms - s.trial_start_ms) / 1000.0;
        if (secs > 0.5 && into_m > 0.0) p.avgKph = into_m / secs * 3.6;
    }
    // The co's arrival: the card's in-time, aimed at the middle of its minute.
    double total_m = 0.0;
    for (const Segment& g : segs) total_m += g.distance_m;
    if (s.sector_card_ms > 0) {
        p.coValid = true;
        p.coRef = segs.back().type == SEG_CO ? segs.back().ref : 0;
        p.coInTimeMs = coInTimeMs(s);
        const double left_ms = static_cast<double>(p.coInTimeMs + SECTOR_AIM_MS - now_ms);
        p.coMinutes = left_ms > 0 ? static_cast<long>(left_ms / 60000.0) : 0;
        if (left_ms > 0 && total_m > stage_m)
            p.coKph = static_cast<int>(std::lround((total_m - stage_m) / 1000.0 / (left_ms / 3600000.0)));
    }
    return p;
}
