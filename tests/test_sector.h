#ifndef TEST_SECTOR_H
#define TEST_SECTOR_H

#include "test_framework.h"
#include "../sector.h"
#include "../calculations.h"
#include "../config_file.h"
#include "../rally_state.h"
#include "../stage_rows.h"
#include <cmath>
#include <cstdio>

// RB-NAV-44: the italian sector, on the Mugello 2024 sector 1 rows (calibration
// 1,000,000: one count is one metre). Rows: navi 380 m; pc1 180 m in 31 s;
// pc2 60 m in 10 s; pc3 110 m in 19 s; navi 16,500 m; pc4 50 m in 9 s; pc5 310 m in 46 s.
class TestSector {
    static Segment row(int type, double m, double pc_s, int ref, double kph = 0.0) {
        Segment s; s.type = type; s.distance_m = m; s.pc_duration_s = pc_s; s.ref = ref;
        s.target_speed_kph = kph;
        return s;
    }
    static std::vector<Segment> mugello() {
        return { row(SEG_NAVI, 380, 0, 0, 30.0), row(SEG_PC, 180, 31, 1), row(SEG_PC, 60, 10, 2),
                 row(SEG_PC, 110, 19, 3), row(SEG_NAVI, 16500, 0, 0, 30.0), row(SEG_PC, 50, 9, 4),
                 row(SEG_PC, 310, 46, 5) };
    }
    // A stage running from t = 0 in the first row.
    static RallyState running(long sector_s = 10800) {
        RallyState s;
        s.calibration = 1000000;
        s.counters = true;
        s.rally_type = 2;
        s.segments = mugello();
        applySectorRules(s.segments, sector_s);
        recomputeSegmentCounts(s.segments, s.calibration);
        s.stage_segments = s.segments;
        s.segment_current_number = 0;
        s.stage_complete = false;
        s.total_start_cntr1 = s.total_start_cntr2 = 0;
        s.total_start_time_ms = 0;
        s.segment_start_cntr1 = s.segment_start_cntr2 = 0;
        s.segment_start_time_ms = 0;
        s.sector_card_ms = sector_s * 1000L;
        return s;
    }
    static int64_t stageCounts(const RallyState& s, uint64_t c) {
        return correctedDistanceCounts(calculateDistanceCounts(s, c, c, s.total_start_cntr1, s.total_start_cntr2),
                                       s.total_distance_adjust_cm, s.calibration);
    }
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Italian sector (RB-NAV-44)");

        suite->addTest("a legs-only stage is not a sector and no tube applies", []() {
            RallyState s;
            s.calibration = 1000000; s.counters = true;
            Segment a, b; a.distance_m = 1000; a.target_speed_kph = 36; b = a;
            s.segments = {a, b};
            recomputeSegmentCounts(s.segments, s.calibration);
            s.stage_segments = s.segments; s.segment_current_number = 0; s.stage_complete = false;
            const int64_t t0 = s.total_start_time_ms; const long cur = s.segment_current_number;
            const bool applied = applySectorNext(s, 500, 500, 99999);
            return !stageIsSector(s.segments) && !applied && s.total_start_time_ms == t0
                && s.segment_current_number == cur && !s.sector_undo.valid && !s.last_pc_error.valid;
        });

        suite->addTest("rules: every row waits for next and changes speed; the pc speed comes from its time", []() {
            auto v = mugello();
            applySectorRules(v, 10800);
            bool ok = true;
            for (const Segment& s : v) ok = ok && !s.autoNext && s.changesSpeed;
            // pc1: 180 m in 31 s
            ok = ok && std::fabs(v[1].target_speed_kph - 180.0 / 31.0 * 3.6) < 0.001;
            // pc22 on the card: 0.94 km in 2:12 prints 25.64
            Segment p = row(SEG_PC, 940, 132, 22);
            std::vector<Segment> w = {p};
            applySectorRules(w, -1);
            return ok && std::fabs(w[0].target_speed_kph - 25.636) < 0.001;
        });

        suite->addTest("a boundary into a pc is never crossed by the odometer", []() {
            RallyState s = running();
            return segmentIndexAtStageCounts(s.segments, 400) == 0 && segmentIndexAtStageCounts(s.segments, 17000) == 0;
        });

        suite->addTest("untimed rows get the speed that fits the sector duration", []() {
            auto v = mugello();
            applySectorRules(v, 10800);
            const double timed = 31 + 10 + 19 + 9 + 46;                       // 115 s
            const double kph = (380 + 16500) / 1000.0 / ((10800 - timed) / 3600.0);
            return std::fabs(v[0].target_speed_kph - kph) < 0.0001 && std::fabs(v[4].target_speed_kph - kph) < 0.0001;
        });

        suite->addTest("no sector duration keeps the typed speed and divides by nothing", []() {
            auto v = mugello();
            applySectorRules(v, -1);
            return v[0].target_speed_kph == 30.0 && v[4].target_speed_kph == 30.0;
        });

        suite->addTest("a duration shorter than the timed rows keeps the typed speed", []() {
            auto v = mugello();
            applySectorRules(v, 100);
            return v[0].target_speed_kph == 30.0;
        });

        suite->addTest("the whole Mugello-style sector's co row reaches its set duration", []() {
            // navi 16.88 km + five pcs; the untimed speed is exact, so elapsed = the sector duration.
            auto v = mugello();
            applySectorRules(v, 10800);
            double total = 0;
            for (const Segment& s : v) total += rowSeconds(s);
            return std::fabs(total - 10800.0) < 0.01;
        });

        suite->addTest("rowStartsTube: pc, and the first row of a pm", []() {
            std::vector<Segment> v = { row(SEG_NAVI, 100, 0, 0, 30), row(SEG_PC, 50, 9, 1), row(SEG_PM, 50, 0, 1, 30),
                                       row(SEG_PM, 50, 0, 1, 30), row(SEG_PM, 50, 0, 2, 30), row(SEG_CT, 50, 0, 1, 30) };
            return !rowStartsTube(v, 0) && rowStartsTube(v, 1) && rowStartsTube(v, 2) && !rowStartsTube(v, 3)
                && rowStartsTube(v, 4) && !rowStartsTube(v, 5) && !rowStartsTube(v, 99);
        });

        suite->addTest("tube pressed late at pc1: the pc clock reads zero there and the stage is pinned to the card", []() {
            RallyState s = running();
            const bool applied = applySectorNext(s, 385, 385, 70000);
            bool ok = false;
            const double ab = trialAheadSeconds(s, 385, 385, 70000, &ok);
            return applied && ok && std::fabs(ab) < 0.01 && std::llabs(stageCounts(s, 385) - 380) <= 1
                && s.segment_current_number == 1 && s.trial_start_ms == 70000 && s.trial_start_m == 380.0;
        });

        suite->addTest("the italian next and its undo are gated on the rally type as well as the stage", []() {
            RallyState g = running(); g.rally_type = 0;
            const bool refused = !applySectorNext(g, 385, 385, 70000) && g.segment_current_number == 0;
            RallyState s = running();
            applySectorNext(s, 385, 385, 70000);
            RallyState u = s; u.rally_type = 0;
            const bool undoRefused = !undoSectorTube(u) && u.segment_current_number == 1;
            return refused && undoRefused && undoSectorTube(s);
        });

        suite->addTest("pc1 is then reckoned from its tube, not from the sector start", []() {
            RallyState s = running();
            applySectorNext(s, 385, 385, 70000);
            bool ok = false;
            const double ab = trialAheadSeconds(s, 475, 475, 70000 + 15500, &ok);
            return ok && std::fabs(ab) < 0.05;
        });

        suite->addTest("tube pressed 5 m early or late leaves later rows' starts where the card has them", []() {
            RallyState s = running();
            applySectorNext(s, 375, 375, 60000);                       // 5 m early
            const int64_t at1 = segmentStartStageCounts(s.stage_segments, 2);
            RallyState t = running();
            applySectorNext(t, 385, 385, 70000);                       // 5 m late
            return segmentStartStageCounts(t.stage_segments, 2) == at1 && std::llabs(stageCounts(s, 375) - 380) <= 1;
        });

        suite->addTest("prev after a tube restores everything", []() {
            RallyState s = running();
            s.trip_start_cntr1 = 11; s.trip_start_cntr2 = 12; s.trip_start_time_ms = 5; s.trip_distance_adjust_cm = 700;
            s.total_distance_adjust_cm = 250; s.segment_start_adjust_cm = 100;
            RallyState before = s;
            applySectorNext(s, 385, 385, 70000);
            const bool undone = undoSectorTube(s);
            return undone && s.segment_current_number == before.segment_current_number
                && s.total_start_time_ms == before.total_start_time_ms
                && s.total_distance_adjust_cm == before.total_distance_adjust_cm
                && s.segment_start_cntr1 == before.segment_start_cntr1 && s.segment_start_adjust_cm == before.segment_start_adjust_cm
                && s.segment_start_time_ms == before.segment_start_time_ms
                && s.trip_start_cntr1 == 11 && s.trip_start_time_ms == 5 && s.trip_distance_adjust_cm == 700
                && s.trip_history_m == before.trip_history_m && !s.sector_undo.valid
                && s.trial_start_m == before.trial_start_m && s.trial_start_ms == before.trial_start_ms;
        });

        suite->addTest("prev after an untimed-line next restores everything, the pc clock included", []() {
            RallyState s = running();
            applySectorNext(s, 380, 380, 70000);
            applySectorNext(s, 560, 560, 101000);
            applySectorNext(s, 620, 620, 111000);                        // into pc3
            RallyState before = s;
            applySectorNext(s, 790, 790, 140000);                        // pc3 -> navi, 60 m past its end
            const bool left = s.trial_start_m < 0.0 && s.segment_current_number == 4;
            const bool undone = undoSectorTube(s);
            return left && undone && s.segment_current_number == 3 && s.trial_start_m == 620.0
                && s.trial_start_ms == 111000 && s.total_distance_adjust_cm == before.total_distance_adjust_cm
                && s.segment_start_time_ms == before.segment_start_time_ms && s.last_pc_error.ref == before.last_pc_error.ref;
        });

        suite->addTest("next never moves the sector clock or a row length", []() {
            RallyState s = running();
            s.total_start_time_ms = 12345;
            const RallyState before = s;
            applySectorNext(s, 400, 400, 70000);                         // into pc1, 20 m late
            applySectorNext(s, 580, 580, 101000);                        // pc1 -> pc2
            applySectorNext(s, 640, 640, 111000);                        // pc2 -> pc3
            applySectorNext(s, 790, 790, 140000);                        // pc3 -> navi, 60 m past its end
            bool same = s.total_start_time_ms == 12345 && s.sector_card_ms == before.sector_card_ms
                && s.stage_segments.size() == before.stage_segments.size();
            for (size_t i = 0; same && i < s.stage_segments.size(); i++)
                same = s.stage_segments[i].distance_m == before.stage_segments[i].distance_m
                    && s.stage_segments[i].distance_counts == before.stage_segments[i].distance_counts;
            return same;
        });

        suite->addTest("next pins sector total to the card line, pc and navi alike", []() {
            RallyState s = running();
            applySectorNext(s, 400, 400, 70000);
            const bool pc = stageCounts(s, 400) == 380;
            applySectorNext(s, 580, 580, 101000);
            applySectorNext(s, 640, 640, 111000);
            applySectorNext(s, 790, 790, 140000);                        // pc3 ends at 730
            return pc && stageCounts(s, 790) == 730;
        });

        suite->addTest("a pc start press never moves the sector figure", []() {
            RallyState s = running();
            s.sector_start_ms = 0;
            bool ok = false;
            const double before = sectorAheadSeconds(s, 380, 380, 40000, &ok);
            applySectorNext(s, 380, 380, 40000);
            const double after = sectorAheadSeconds(s, 380, 380, 40000, &ok);
            return ok && std::fabs(before - after) < 0.001;
        });

        suite->addTest("a pm continuation keeps the pm clock; a navi after it clears it", []() {
            RallyState s = running();
            s.stage_segments = { row(SEG_NAVI, 1000, 0, 0, 36.0), row(SEG_PM, 1000, 0, 1, 40.0),
                                 row(SEG_PM, 1000, 0, 1, 40.0), row(SEG_NAVI, 1000, 0, 0, 36.0) };
            recomputeSegmentCounts(s.stage_segments, s.calibration);
            applySectorNext(s, 1000, 1000, 100000);
            const bool start = s.trial_start_ms == 100000 && s.trial_start_m == 1000.0;
            applySectorNext(s, 2000, 2000, 190000);
            const bool kept = s.trial_start_ms == 100000 && s.trial_start_m == 1000.0 && s.segment_current_number == 2;
            applySectorNext(s, 3000, 3000, 280000);
            return start && kept && s.trial_start_m < 0.0 && s.segment_current_number == 3;
        });

        suite->addTest("a left-over italian undo never fires on a greek stage", []() {
            RallyState s = running();
            applySectorNext(s, 385, 385, 70000);                          // undo valid, index 0
            Segment a; a.distance_m = 1000; a.target_speed_kph = 36;
            s.stage_segments = {a, a};                                   // a greek stage now, on segment 1
            recomputeSegmentCounts(s.stage_segments, s.calibration);
            s.segment_current_number = 1;
            const bool greek = !undoSectorTube(s) && !sectorUndoAvailable(s);
            RallyState t = running();
            applySectorNext(t, 385, 385, 70000);
            t.total_start_time_ms = 0;
            markSectorStart(t);                                          // a new sector starts
            return greek && !t.sector_undo.valid;
        });

        suite->addTest("prev keeps a -10 made after the next", []() {
            RallyState s = running();
            applySectorNext(s, 385, 385, 70000);                          // pinned: 385 reads 380
            s.total_distance_adjust_cm -= 1000;                          // -10
            s.trip_distance_adjust_cm -= 1000;
            const long trip_before = s.trip_distance_adjust_cm;
            RallyState t = running();
            const long trip_unmoved = t.trip_distance_adjust_cm;
            undoSectorTube(s);
            return stageCounts(s, 385) == 375 && s.trip_distance_adjust_cm == trip_unmoved - 1000 && trip_before != 0;
        });

        suite->addTest("nothing to undo before a tube, and not twice", []() {
            RallyState s = running();
            const bool none = !undoSectorTube(s);
            applySectorNext(s, 385, 385, 70000);
            const bool once = undoSectorTube(s);
            return none && once && !undoSectorTube(s);
        });

        suite->addTest("set navi: reads the typed distance, leaves Total and Trip alone", []() {
            RallyState s = running();
            s.total_distance_adjust_cm = 700; s.trip_distance_adjust_cm = 300;
            const bool ok = setOdometerMetres(s, 500, 500, 120.0);
            return ok && countsToCentimeters(odometerCounts(s, 500, 500), s.calibration) == 12000
                && s.total_distance_adjust_cm == 700 && s.trip_distance_adjust_cm == 300;
        });

        suite->addTest("set navi: a negative distance changes nothing; reset zeroes it", []() {
            RallyState s = running();
            s.odometer_adjust_cm = 40;
            const bool refused = !setOdometerMetres(s, 500, 500, -1.0) && s.odometer_adjust_cm == 40;
            setOdometerMetres(s, 500, 500, 120.0);
            zeroOdometer(s, 500, 500);
            return refused && odometerCounts(s, 500, 500) == 0;
        });

        suite->addTest("ahead / behind: from the middle of the minute, 60 s early reads +60, 120 s late -120", []() {
            RallyState s = running();
            s.sector_start_ms = 0;
            bool ok = false;
            const double ideal = idealSecondsToReachDistance(s.stage_segments, 380.0);
            const double early = sectorAheadSeconds(s, 380, 380, 30000 + static_cast<int64_t>((ideal - 60.0) * 1000), &ok);
            const bool a = ok && std::fabs(early - 60.0) < 0.01;
            const double late = sectorAheadSeconds(s, 380, 380, 30000 + static_cast<int64_t>((ideal + 120.0) * 1000), &ok);
            return a && ok && std::fabs(late + 120.0) < 0.01;
        });

        suite->addTest("ahead / behind: the position is sector total, navi does not move it", []() {
            RallyState s = running();
            s.sector_start_ms = 0;
            bool ok = false;
            const double before = sectorAheadSeconds(s, 380, 380, 20000, &ok);
            setOdometerMetres(s, 380, 380, 5000.0);
            const double after = sectorAheadSeconds(s, 380, 380, 20000, &ok);
            s.total_distance_adjust_cm = -1000;                         // -10 on sector total
            const double minus10 = sectorAheadSeconds(s, 380, 380, 20000, &ok);
            return ok && std::fabs(before - after) < 0.001 && minus10 < before - 0.1;
        });

        suite->addTest("ahead / behind: invalid with no sector", []() {
            RallyState g;
            bool gok = true;
            sectorAheadSeconds(g, 0, 0, 0, &gok);
            return !gok;
        });

        suite->addTest("countdown in pc1 runs from its tube: 10 s in reads 21", []() {
            RallyState s = running();
            s.segment_current_number = 1;
            s.trial_start_ms = 70000; s.trial_start_m = 380.0;
            bool ok = false;
            const double left = sectorSecondsToLine(s, 70000 + 10000, &ok);
            return ok && std::fabs(left - 21.0) < 0.01;
        });

        suite->addTest("countdown on the co row is the co in-time's middle minus now", []() {
            RallyState s = running();
            s.segments.push_back(row(SEG_CO, 1000, 0, 2, 30.0));
            applySectorRules(s.segments, 10800);
            recomputeSegmentCounts(s.segments, s.calibration);
            s.stage_segments = s.segments;
            s.segment_current_number = static_cast<long>(s.stage_segments.size()) - 1;
            s.sector_start_ms = 0;
            bool ok = false;
            const double left = sectorSecondsToLine(s, 30000 + 10800000 - 600000, &ok);
            return ok && std::fabs(left - 600.0) < 0.01;
        });

        suite->addTest("trial figure: a pm three rows long counts from its first tube", []() {
            RallyState s = running();
            s.stage_segments = { row(SEG_NAVI, 1000, 0, 0, 36.0), row(SEG_PM, 1000, 0, 1, 40.0),
                                 row(SEG_PM, 1000, 0, 1, 40.0), row(SEG_PM, 1000, 0, 1, 40.0) };
            recomputeSegmentCounts(s.stage_segments, s.calibration);
            s.segment_current_number = 3;
            s.trial_start_ms = 500000; s.trial_start_m = 1000.0;
            bool ok = false, lok = false;
            const double ab = trialAheadSeconds(s, 3500, 3500, 500000 + 225000, &ok);
            const double left = sectorSecondsToLine(s, 500000 + 225000, &lok);
            return ok && lok && std::fabs(ab) < 0.01 && std::fabs(left - 45.0) < 0.01;
        });

        suite->addTest("italian ahead: the trial figure inside a pc, the sector figure outside", []() {
            RallyState s = running();
            s.sector_start_ms = 0;
            bool ok = false, sok = false, tok = false;
            const double out = italianAheadSeconds(s, 300, 300, 40000, &ok);
            const double sec = sectorAheadSeconds(s, 300, 300, 40000, &sok);
            const bool a = ok && sok && std::fabs(out - sec) < 0.001;
            s.segment_current_number = 1;
            s.trial_start_ms = 70000; s.trial_start_m = 380.0;
            const double in = italianAheadSeconds(s, 470, 470, 85500, &ok);
            const double tr = trialAheadSeconds(s, 470, 470, 85500, &tok);
            return a && ok && tok && std::fabs(in - tr) < 0.001 && std::fabs(in) < 0.05;
        });

        suite->addTest("countdown on a navi row: seconds to the end of the row by the schedule", []() {
            RallyState s = running();
            bool ok = false;
            const double left = sectorSecondsToLine(s, 1000, &ok);
            return ok && left > 0.0;
        });

        suite->addTest("pc error: time against the card and distance against its progr", []() {
            RallyState s = running();
            applySectorNext(s, 380, 380, 70000);                         // into pc1, pinned at 380
            // 31.4 s later, 2 m past the card's 560 m: next into pc2 (also a tube)
            applySectorNext(s, 562, 562, 70000 + 31400);
            return s.last_pc_error.valid && s.last_pc_error.ref == 1
                && std::fabs(s.last_pc_error.seconds - 0.4) < 0.001 && s.last_pc_error.metres == 2;
        });

        suite->addTest("leaving a pc onto a navi row records its error from the pc's tube", []() {
            RallyState s = running();
            applySectorNext(s, 380, 380, 70000);
            applySectorNext(s, 560, 560, 70000 + 31000);
            applySectorNext(s, 620, 620, 70000 + 41000);                 // pc2 -> pc3 tube
            const bool applied = applySectorNext(s, 732, 732, 70000 + 61000);   // pc3 (110 m, 19 s) -> navi
            return applied && s.last_pc_error.valid && s.last_pc_error.ref == 3 && s.segment_current_number == 4
                && std::fabs(s.last_pc_error.seconds - 1.0) < 0.001 && s.last_pc_error.metres == 2;
        });

        suite->addTest("italian add distance: a navi row splits into two navis", []() {
            auto v = mugello();
            const size_t n = v.size();
            std::string split = insertSectorDistances(v, 0, {200});                 // inside navi 1 (0-380)
            return split.empty() && v.size() == n + 1 && v[0].type == SEG_NAVI && v[1].type == SEG_NAVI
                && v[0].distance_m == 200 && v[1].distance_m == 180;
        });

        suite->addTest("italian add distance: a pc splits into two pcs, time shared, later pcs renumbered", []() {
            auto v = mugello();
            const size_t n = v.size();
            std::string note = insertSectorDistances(v, 0, {470});                  // inside pc1 (380-560, 31 s)
            return note.empty() && v.size() == n + 1
                && v[1].type == SEG_PC && v[1].ref == 1 && v[1].distance_m == 90
                && v[2].type == SEG_PC && v[2].ref == 2 && v[2].distance_m == 90
                && v[1].pc_duration_s + v[2].pc_duration_s == 31 && v[1].pc_duration_s == 16
                && !v[1].autoNext && !v[2].autoNext && v[2].changesSpeed
                && v[3].ref == 3 && v[4].ref == 4 && v[6].ref == 5 && v[7].ref == 6;
        });

        suite->addTest("italian add distance: two distances in one pc share its card time over three pcs", []() {
            auto v = mugello();
            const size_t n = v.size();
            insertSectorDistances(v, 0, {440, 500});                                // pc1 is 380-560, 31 s
            const double d1 = v[1].distance_m, d2 = v[2].distance_m, d3 = v[3].distance_m;
            return v.size() == n + 2 && v[1].ref == 1 && v[2].ref == 2 && v[3].ref == 3
                && d1 == 60 && d2 == 60 && d3 == 60
                && v[1].pc_duration_s + v[2].pc_duration_s + v[3].pc_duration_s == 31
                && v[1].pc_duration_s == 10 && v[2].pc_duration_s == 10 && v[3].pc_duration_s == 11
                && v[4].ref == 4 && !v[3].autoNext;
        });

        suite->addTest("italian add distance: a pm splits into two rows of the same pm", []() {
            auto v = mugello();
            v[4].type = SEG_PM; v[4].ref = 1; v[4].target_speed_kph = 39.0;          // 732-17,232
            const size_t n = v.size();
            std::string note = insertSectorDistances(v, 0, {10000});
            return note.empty() && v.size() == n + 1
                && v[4].type == SEG_PM && v[5].type == SEG_PM && v[4].ref == 1 && v[5].ref == 1
                && v[4].target_speed_kph == 39.0 && v[5].target_speed_kph == 39.0
                && !v[4].autoNext && !v[5].autoNext && v[6].ref == 4;
        });

        suite->addTest("italian add distance past the end copies the type and numbers the ref", []() {
            auto v = mugello();                                                     // ends with pc5
            insertSectorDistances(v, 0, {17800});                                    // end is 17,590
            return v.size() == 8 && v[7].type == SEG_PC && v[7].ref == 6 && !v[7].autoNext && v[7].changesSpeed;
        });

        suite->addTest("typed durations: seconds, m:ss and h:mm:ss", []() {
            return parseDurationSeconds("31") == 31 && parseDurationSeconds("0:31") == 31 && parseDurationSeconds("2:12") == 132
                && parseDurationSeconds("1:02:03") == 3723 && parseDurationSeconds("") == -1 && parseDurationSeconds("x") == -1
                && parseDurationSeconds("1:") == -1;
        });

        suite->addTest("an italian start counts from the minute pressed", []() {
            const int64_t t = (11 * 3600L + 30 * 60 + 40) * 1000L;
            return italianStageStartMs(t) == (11 * 3600L + 30 * 60) * 1000L && italianStageStartMs(60000) == 60000;
        });

        suite->addTest("markSectorStart floors the start and notes the sector start; greek is untouched", []() {
            RallyState s = running();
            s.total_start_time_ms = (11 * 3600L + 30 * 60 + 40) * 1000L;
            markSectorStart(s);
            // The start time is 30 s through the minute pressed; the card's departure minute stays the minute.
            const bool it = s.total_start_time_ms == (11 * 3600L + 30 * 60 + 30) * 1000L
                && s.sector_start_ms == (11 * 3600L + 30 * 60) * 1000L;
            RallyState g = running();
            g.rally_type = 0;
            g.total_start_time_ms = 40000;
            markSectorStart(g);
            return it && g.total_start_time_ms == 40000;
        });

        suite->addTest("sector clock: go at 11:30:51 zeroes at 11:30:30, co in-time from the card", []() {
            RallyState s = running();
            s.total_start_time_ms = s.segment_start_time_ms = 41451000;   // 11:30:51
            markSectorStart(s);
            return s.sector_start_ms == 41400000 && sectorClockZeroMs(s) == 41430000 && s.total_start_time_ms == 41430000
                && s.sector_card_ms == 10800000 && coInTimeMs(s) == 41400000 + 10800000
                && s.trial_start_m < 0.0;
        });

        suite->addTest("a sector opening on a pc times the pc from the real press", []() {
            RallyState s = running();
            s.stage_segments.erase(s.stage_segments.begin());            // opens on pc1
            s.total_start_time_ms = s.segment_start_time_ms = 41440000;   // 11:30:40
            markSectorStart(s);
            return s.trial_start_ms == 41440000 && s.trial_start_m == 0.0 && s.sector_start_ms == 41400000;
        });

        suite->addTest("early departure: armed at 11:29:20, the sector starts at the appointed minute", []() {
            RallyState s = running();
            s.sector_start_ms = 999;                                      // the previous sector's
            s.total_start_cntr1 = s.total_start_cntr2 = 50;               // distance zeroed at arming
            s.segment_start_time_ms = 41360000;                           // 11:29:20, the arming press
            markSectorOdometer(s, 50, 50);
            s.total_start_time_ms = s.trip_start_time_ms = 41400000;      // the minute: zeroTimeBaselines
            s.segment_start_time_ms = 41400000;
            markSectorStart(s);
            return s.sector_start_ms == 41400000 && s.sector_card_ms == 10800000 && s.trial_start_m < 0.0;
        });

        suite->addTest("tags, labels and ref numbering", []() {
            auto v = mugello();
            Segment a = row(SEG_PC, 10, 5, 5);
            const int n = nextRef(v, SEG_PC);
            return sectorTag(a) == "pc5" && typeLabel(SEG_PM) == "pm" && sectorTag(row(SEG_NAVI, 1, 0, 0)) == "navi"
                && sectorTag(Segment()) == "" && n == 6 && nextRef(v, SEG_PM) == 1
                && stepSegmentType(SEG_NAVI) == SEG_PC && stepSegmentType(SEG_CO) == SEG_NAVI;
        });

        suite->addTest("navi total is independent of the stage baseline and zeroes on its own", []() {
            RallyState s;
            s.calibration = 1000000; s.counters = true;
            s.odometer_start_cntr1 = s.odometer_start_cntr2 = 1000;
            s.total_start_cntr1 = s.total_start_cntr2 = 3000;
            const int64_t before = odometerCounts(s, 5000, 5000);
            zeroOdometer(s, 5000, 5000);
            return before == 4000 && odometerCounts(s, 5000, 5000) == 0 && s.odometer_adjust_cm == 0
                && s.total_start_cntr1 == 3000;
        });

        suite->addTest("panel on a pc: tags, countdown and the co arrival", []() {
            RallyState s = running();
            s.sector_start_ms = 0;
            s.segment_current_number = 1;
            s.trial_start_ms = 70000; s.trial_start_m = 380.0;
            SectorPanel p = sectorPanel(s, 70000 + 10000, 438);
            return p.running && p.kind == SectorPanel::PC && p.inTag == "pc1" && p.nextTag == "pc2"
                && p.nextAtM == 560 && p.secondsValid && std::fabs(p.secondsToLine - 21.0) < 0.01
                && p.coValid && p.coInTimeMs == 10800000 && std::fabs(p.avgKph - 58.0 / 10.0 * 3.6) < 0.01;
        });

        suite->addTest("panel on a navi sub: metres to the line, no schedule time", []() {
            RallyState s = running();
            SectorPanel p = sectorPanel(s, 1000, 100);
            return p.kind == SectorPanel::NAVI && p.inTag == "navi" && p.nextTag == "pc1"
                && !p.secondsValid && p.metresToGo == 280 && p.nextAtM == 380;
        });

        suite->addTest("panel: 40 m past the line", []() {
            RallyState s = running();
            const long at = sectorPanel(s, 1000, 100).nextAtM;
            SectorPanel p = sectorPanel(s, 1000, at + 40);
            return p.metresPast == 40 && p.metresToGo == 0;
        });

        suite->addTest("panel: before the line nothing is past", []() {
            RallyState s = running();
            const long at = sectorPanel(s, 1000, 100).nextAtM;
            SectorPanel p = sectorPanel(s, 1000, at - 10);
            return p.metresPast == 0 && p.metresToGo == 10;
        });

        suite->addTest("panel: past the end of the last row", []() {
            RallyState s = running();
            s.segment_current_number = static_cast<int>(s.stage_segments.size()) - 1;
            const long at = sectorPanel(s, 1000, 100).nextAtM;
            SectorPanel p = sectorPanel(s, 1000, at + 40);
            return p.metresPast == 40;
        });

        suite->addTest("pc countdown beeps: 30 s, 15 s, 5-4-3-2-1, short beep on 0", []() {
            auto b = pcCountdownBeep;
            return b(30.05, 29.95) == PcBeep::Tick && b(15.01, 14.99) == PcBeep::Tick
                && b(5.02, 4.98) == PcBeep::Tick && b(1.01, 0.99) == PcBeep::Tick
                && b(0.01, -0.01) == PcBeep::Zero
                && b(29.9, 29.8) == PcBeep::None && b(10.0, 6.0) == PcBeep::None
                && b(-0.5, -0.6) == PcBeep::None                 // past the line: silent
                && b(1.01, -0.01) == PcBeep::Zero;               // two crossed in one step: the 0 wins
        });

        suite->addTest("pc countdown seconds: only inside a pc, from its own tube", []() {
            RallyState s = running();
            // Find a pc row, start its clock at the tube.
            long pc = -1;
            for (size_t i = 0; i < s.stage_segments.size(); i++)
                if (s.stage_segments[i].type == SEG_PC) { pc = static_cast<long>(i); break; }
            if (pc < 0) return false;
            s.segment_current_number = pc;
            s.trial_start_ms = 1000000;
            double tube_m = 0.0;                                // the pc's start tube
            for (long i = 0; i < pc; i++) tube_m += s.stage_segments[static_cast<size_t>(i)].distance_m;
            s.trial_start_m = tube_m;
            bool ok = false;
            const double t = pcCountdownSeconds(s, 1000000 + 5000, &ok);
            const double card = s.stage_segments[static_cast<size_t>(pc)].pc_duration_s;
            const bool in_pc = ok && std::fabs(t - (card - 5.0)) < 0.01;
            s.segment_current_number = 0;                      // the navi before it: none
            pcCountdownSeconds(s, 1005000, &ok);
            return in_pc && !ok;
        });

        suite->addTest("readout keys: italian reads sector total / sub / navi, greek unchanged", []() {
            return std::string(readoutKeyName(2, 1)) == "sub" && std::string(readoutKeyName(2, 0)) == "sector\ntotal"
                && std::string(readoutKeyName(2, 2)) == "navi" && std::string(readoutKeyName(0, 1)) == "trip"
                && std::string(readoutKeyName(0, 0)) == "total" && std::string(readoutKeyName(0, 2)) == "countdown"
                && std::string(readoutKeyName(0, 3)) == "next\nspeed" && std::string(readoutKeyName(1, 3)) == "next\nspeed"
                && std::string(readoutKeyName(2, 3)) == "next\nsubstage"
                && std::string(readoutKeyName(0, 4)) == "prev\nspeed" && std::string(readoutKeyName(2, 4)) == "prev\nsubstage";
        });

        suite->addTest("idle preview only for a sector, never for a greek stage in italian mode", []() {
            RallyState s = running(); s.stage_complete = true;
            const bool sector = sectorPreviewWanted(s);
            Segment a; a.distance_m = 540; a.target_speed_kph = 36; s.segments = {a, a};
            const bool legs = sectorPreviewWanted(s);
            RallyState r = running();                                    // a sector under way
            return sector && !legs && !sectorPreviewWanted(r);
        });

        suite->addTest("needle: rests on a navi sub however far ahead, follows the pc inside one", []() {
            RallyState s = running(); s.sector_start_ms = 0;
            bool ok = true;
            const double navi = italianNeedleSeconds(s, 300, 300, 1000, &ok);
            const bool rest = !ok && navi == 0.0;
            s.segment_current_number = 1; s.trial_start_ms = 70000; s.trial_start_m = 380.0;
            bool tok = false, nok = false;
            const double tr = trialAheadSeconds(s, 470, 470, 85500, &tok);
            const double in = italianNeedleSeconds(s, 470, 470, 85500, &nok);
            return rest && nok && tok && std::fabs(in - tr) < 0.001;
        });

        suite->addTest("gauge figure (driver and phone): italian navi rests at 0, pc follows its tube, greek unchanged", []() {
            RallyState s = running(); s.sector_start_ms = 0;
            const bool navi = gaugeAheadSeconds(s, 300, 300, 1000, stageCounts(s, 300)) == 0.0;
            s.segment_current_number = 1; s.trial_start_ms = 70000; s.trial_start_m = 380.0;
            bool tok = false;
            const double tr = trialAheadSeconds(s, 470, 470, 85500, &tok);
            const bool pc = std::fabs(gaugeAheadSeconds(s, 470, 470, 85500, stageCounts(s, 470)) - tr) < 0.001;
            RallyState g = running(); g.rally_type = 0; g.ahead_behind_zero_offset_ms = 5000;
            const double want = calculateAheadBehindFromStageStart(g, 40000, stageCounts(g, 300)) + 5.0;
            const bool greek = std::fabs(gaugeAheadSeconds(g, 300, 300, 40000, stageCounts(g, 300)) - want) < 0.001;
            return navi && pc && greek;
        });

        suite->addTest("minutes in hand: 25 min reads 25, two hours stops at 99", []() {
            return minutesInHand(1500.0) == 25 && minutesInHand(-1500.0) == 25 && minutesInHand(7200.0) == 99
                && minutesInHand(20.0) == 0;
        });


        suite->addTest("italian fields survive a save and load; an old file loads as legs", []() {
            const char* path = "test_sector_rt.json";
            RallyState a;
            a.segments = mugello();
            a.memory_slots[17].sector_duration_s = 10800;
            a.odometer_start_cntr1 = 42; a.odometer_adjust_cm = -300; a.sector_start_ms = 123456;
            a.sector_card_ms = 10800000; a.trial_start_ms = 654321; a.trial_start_m = 380.0;
            ConfigFile::save(a, path);
            RallyState b;
            ConfigFile::load(b, path);
            std::remove(path);
            const bool rt = b.segments.size() == 7 && b.segments[1].type == SEG_PC && b.segments[1].ref == 1
                && b.segments[1].pc_duration_s == 31 && b.memory_slots[17].sector_duration_s == 10800
                && b.odometer_start_cntr1 == 42 && b.odometer_adjust_cm == -300 && b.sector_start_ms == 123456
                && b.sector_card_ms == 10800000 && b.trial_start_ms == 654321 && b.trial_start_m == 380.0;
            RallyState g;
            Segment x; x.distance_m = 100; g.segments = {x};
            ConfigFile::save(g, path);
            RallyState h;
            ConfigFile::load(h, path);
            std::remove(path);
            return rt && !stageIsSector(h.segments) && h.memory_slots[0].sector_duration_s == -1;
        });

        suite->addTest("an old config seeds navi total from Total", []() {
            const char* path = "test_sector_old.json";
            FILE* f = fopen(path, "w");
            fputs("{\n  \"total_start_cntr1\": 3000,\n  \"total_start_cntr2\": 3100,\n  \"total_distance_adjust_cm\": 500\n}\n", f);
            fclose(f);
            RallyState s;
            ConfigFile::load(s, path);
            std::remove(path);
            return s.odometer_start_cntr1 == 3000 && s.odometer_start_cntr2 == 3100 && s.odometer_adjust_cm == 500;
        });

        suite->addTest("preview: opens on the opening navi", []() {
            RallyState s = running();
            s.stage_complete = true; s.segment_current_number = 3;
            RallyState v = sectorPreviewState(s, 5000);
            return v.segment_current_number == 0 && sectorPanel(v, 5000, 0).kind == SectorPanel::NAVI;
        });

        suite->addTest("preview: a sector that opens with a pc shows pc 1", []() {
            RallyState s = running();
            s.segments.erase(s.segments.begin());
            applySectorRules(s.segments, 10800);
            recomputeSegmentCounts(s.segments, s.calibration);
            s.stage_complete = true;
            RallyState v = sectorPreviewState(s, 5000);
            SectorPanel p = sectorPanel(v, 5000, 0);
            return v.segment_current_number == 0 && p.kind == SectorPanel::PC && p.inTag == "pc1";
        });

        suite->addTest("preview: live state untouched", []() {
            RallyState s = running();
            s.stage_complete = true; s.segment_current_number = 3; s.total_start_time_ms = 123;
            RallyState v = sectorPreviewState(s, 5000);
            (void)v;
            return s.segment_current_number == 3 && s.stage_complete && s.total_start_time_ms == 123;
        });

        suite->addTest("RB-NAV-52: past the card km with no press the italian sector is not ended by distance", []() {
            RallyState s = running();
            s.segment_current_number = 6;   // the last row
            double total = 0; for (const Segment& g : s.stage_segments) total += g.distance_counts;
            return stageDistanceComplete(s.stage_segments, static_cast<int64_t>(total) + 500)
                && !sectorEndsOnDistance(s);
        });

        suite->addTest("RB-NAV-52: next on the last row ends the italian sector", []() {
            RallyState s = running();
            s.segment_current_number = 6;
            bool avail = sectorFinishAvailable(s);
            bool did = applySectorFinish(s);
            return avail && did && s.stage_complete && !sectorFinishAvailable(s) && !sectorUndoAvailable(s);
        });

        suite->addTest("RB-NAV-52: finish is available only on the last row of a running italian sector", []() {
            RallyState s = running();
            bool ok = !sectorFinishAvailable(s);              // first row
            s.segment_current_number = 5; ok = ok && !sectorFinishAvailable(s);
            s.segment_current_number = 6; ok = ok && sectorFinishAvailable(s);
            RallyState g = s; g.rally_type = 0; ok = ok && !sectorFinishAvailable(g);
            RallyState d = s; d.stage_complete = true; ok = ok && !sectorFinishAvailable(d);
            RallyState i = s; i.segment_current_number = -1; ok = ok && !sectorFinishAvailable(i);
            return ok && !applySectorFinish(g);
        });

        suite->addTest("RB-NAV-52: a greek stage still ends on distance", []() {
            RallyState s = running();
            s.rally_type = 0;
            s.segment_current_number = 6;
            return sectorEndsOnDistance(s);
        });

        suite->addTest("RB-NAV-52: an italian roadbook that is not a sector still ends on distance", []() {
            RallyState s;
            s.rally_type = 2;
            Segment a; a.distance_m = 1000; a.target_speed_kph = 36;
            s.stage_segments = {a};
            return sectorEndsOnDistance(s);
        });

        return suite;
    }
};

#endif
