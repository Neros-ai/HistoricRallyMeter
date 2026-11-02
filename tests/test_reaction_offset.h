#ifndef TEST_REACTION_OFFSET_H
#define TEST_REACTION_OFFSET_H

#include "test_framework.h"
#include "../reaction_offset.h"
#include "../surrey.h"
#include "../sector.h"
#include "../calculations.h"
#include "../config_file.h"
#include "../rally_state.h"
#include "../stage_rows.h"
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

// RB-SET-12: the reaction time offset. Pure maths and the state-level helpers the callers use.
class TestReactionOffset {
    static RallyState started(int rally_type, int offset_ms, int64_t press_ms) {
        RallyState s;
        s.calibration = 1000000;   // one count is one metre
        s.rally_type = rally_type;
        s.reaction_offset_ms = offset_ms;
        s.total_start_time_ms = s.trip_start_time_ms = s.segment_start_time_ms = press_ms;
        return s;
    }
    static Segment row(int type, double m, double pc_s, int ref, double kph = 0.0) {
        Segment s; s.type = type; s.distance_m = m; s.pc_duration_s = pc_s; s.ref = ref;
        s.target_speed_kph = kph;
        return s;
    }
    // An italian sector (navi 380 m, pc1 180 m in 31 s, pc2 60 m in 10 s, navi) running in row 0.
    static RallyState sector(int offset_ms) {
        RallyState s;
        s.calibration = 1000000; s.counters = true; s.rally_type = 2;
        s.reaction_offset_ms = offset_ms;
        s.segments = { row(SEG_NAVI, 380, 0, 0, 30.0), row(SEG_PC, 180, 31, 1), row(SEG_PC, 60, 10, 2),
                       row(SEG_NAVI, 5000, 0, 0, 30.0) };
        applySectorRules(s.segments, 10800);
        recomputeSegmentCounts(s.segments, s.calibration);
        s.stage_segments = s.segments;
        s.segment_current_number = 0;
        s.stage_complete = false;
        s.sector_start_ms = 41400000;
        s.total_start_time_ms = sectorClockZeroMs(s);
        s.sector_card_ms = 10800000;
        return s;
    }
    static std::string dump(const char* path) {
        std::ifstream f(path); std::stringstream ss; ss << f.rdbuf(); return ss.str();
    }
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Reaction offset (RB-SET-12)");

        suite->addTest("reaction offset moves the pc clock start", []() {
            // start now into a tube, +80 ms, moving: trial_start_ms is the press less 80 ms.
            RallyState s = sector(80);
            s.trial_start_ms = 41440000; s.trial_start_m = 0.0;
            applyStartPressOffset(s, 10.0);
            return s.trial_start_ms == 41439920;
        });

        suite->addTest("flying start: Trip and Total take the same distance back", []() {
            RallyState s; s.rally_type = 0; s.reaction_offset_ms = 100;
            applyStartPressOffset(s, 10.0);
            return s.total_distance_adjust_cm == 100 && s.trip_distance_adjust_cm == 100;
        });

        suite->addTest("italian tube press: the clock starts 80 ms earlier", []() {
            RallyState s = sector(80);
            applySectorNext(s, 385, 385, pressTimeMs(41440000, 80));
            return s.trial_start_ms == 41439920;
        });

        suite->addTest("italian start without a tube or in a legs-only stage is left alone", []() {
            RallyState s = sector(80);
            s.trial_start_ms = 41440000; s.trial_start_m = -1.0;
            applyStartPressOffset(s, 10.0);
            RallyState g = started(2, 80, 41440000);
            Segment leg; leg.distance_m = 1000; g.stage_segments = {leg};
            g.trial_start_ms = 41440000; g.trial_start_m = 0.0;
            applyStartPressOffset(g, 10.0);
            return s.trial_start_ms == 41440000 && g.trial_start_ms == 41440000;
        });

        suite->addTest("offset zero changes nothing", []() {
            bool ok = true;
            for (int type : {0, 1, 2}) {
                RallyState s = started(type, 0, 5000000);
                s.trial_start_ms = 5000000; s.trial_start_m = 0.0;
                applyStartPressOffset(s, 10.0);
                applyNextBaselineOffset(s, 10.0);
                ok = ok && s.total_start_time_ms == 5000000 && s.segment_start_time_ms == 5000000 &&
                     s.trip_start_time_ms == 5000000 && s.trial_start_ms == 5000000 &&
                     s.total_distance_adjust_cm == 0 && s.sector_start_odometer_cm == 0 &&
                     s.segment_start_adjust_cm == 0 && s.trip_distance_adjust_cm == 0;
            }
            return ok && pressTimeMs(1234, 0) == 1234 && pressBackCounts(10.0, 0, 1000000) == 0 &&
                   boundaryCountsAtPress(-5, 10.0, 0, 1000000) == -5 &&
                   boundaryCountsAtPress(1000, 10.0, 0, 1000000) == 1000;
        });

        suite->addTest("greek next pulls the boundary back", []() {
            // +100 ms at 36 kph (10 m/s): 1.00 m short of the driven distance.
            const double v = reactionSpeedMps(36.0, false);
            Segment a, b; a.distance_m = 2000; b.distance_m = 3000;
            std::vector<Segment> segs = {a, b};
            recomputeSegmentCounts(segs, 1000000);
            const int64_t driven = boundaryCountsAtPress(1005, v, 100, 1000000);
            retimeSegmentBoundaryForward(segs, 0, driven, 1000000);
            return driven == 1004 && segs[0].distance_m == 1004.0 && pressTimeMs(36000000, 100) == 35999900;
        });

        suite->addTest("greek next: the second boundary lands at the real point", []() {
            // 10 m/s, +100 ms (1 m back). Real points: P1 at 999 m, P2 at 1999 m -> segment 1 is 1000 m.
            RallyState s = started(0, 100, 0);
            const double v = 10.0;
            // press 1 at 1000 m driven
            int64_t d1 = boundaryCountsAtPress(
                correctedDistanceCounts(1000, s.total_distance_adjust_cm - s.segment_start_adjust_cm, s.calibration),
                v, 100, s.calibration);
            s.segment_start_adjust_cm = s.total_distance_adjust_cm;   // what on_next_press does
            applyNextBaselineOffset(s, v);
            // press 2 at 2000 m on the counters, 1000 m since press 1's counters
            int64_t d2 = boundaryCountsAtPress(
                correctedDistanceCounts(1000, s.total_distance_adjust_cm - s.segment_start_adjust_cm, s.calibration),
                v, 100, s.calibration);
            return d1 == 999 && d2 == 1000 && s.trip_distance_adjust_cm == 100;
        });

        suite->addTest("negative offset pushes the boundary on", []() {
            return boundaryCountsAtPress(1000, 10.0, -100, 1000000) == 1001;
        });

        suite->addTest("surrey start at a waypoint", []() {
            // +100 ms at 50 kph: stage distance baseline 1.39 m back, stage clock 100 ms earlier.
            RallyState s = started(1, 100, 8000000);
            s.sector_start_odometer_cm = 500000;
            applyStartPressOffset(s, reactionSpeedMps(50.0, false));
            return s.total_start_time_ms == 7999900 && s.segment_start_time_ms == 7999900 &&
                   s.total_distance_adjust_cm == 139 && s.sector_start_odometer_cm == 500000 - 139;
        });

        suite->addTest("greek start now as a flying start", []() {
            RallyState s = started(0, 100, 8000000);
            applyStartPressOffset(s, 10.0);
            return s.total_start_time_ms == 7999900 && s.total_distance_adjust_cm == 100 &&
                   s.sector_start_odometer_cm == 0;
        });

        suite->addTest("a standing start is untouched; 5 kph and over counts as moving", []() {
            RallyState a = started(0, 100, 8000000);
            applyStartPressOffset(a, reactionSpeedMps(4.9, false));
            RallyState z = started(0, 100, 8000000);
            applyStartPressOffset(z, reactionSpeedMps(-1.0, false));   // no reading
            RallyState b = started(0, 100, 8000000);
            applyStartPressOffset(b, reactionSpeedMps(5.0, false));
            return a.total_start_time_ms == 8000000 && a.total_distance_adjust_cm == 0 &&
                   z.total_start_time_ms == 8000000 && z.total_distance_adjust_cm == 0 &&
                   b.total_start_time_ms == 7999900 && b.total_distance_adjust_cm == 14;
        });

        suite->addTest("surrey time taken is unchanged", []() {
            RallyState a = started(1, 0, 1000000), b = started(1, 100, 1000000);
            for (RallyState* s : {&a, &b}) {
                Segment g; g.distance_m = 1000; g.target_speed_kph = 36;
                s->stage_segments = {g};
                s->segment_current_number = 0;
                s->stage_complete = false;
                s->last_memory_slot = 1;
                applyStartPressOffset(*s, 10.0);
            }
            const bool fa = applySurreyFinish(a, pressTimeMs(1060000, 0));
            const bool fb = applySurreyFinish(b, pressTimeMs(1060000, 100));
            return fa && fb && a.time_taken_ms == 60000 && b.time_taken_ms == 60000;
        });

        suite->addTest("italian pc error is unchanged when both tube presses carry the offset", []() {
            // pc1 pressed in at 70000 (385 m), out at 70000 + 31500 ms (565 m, into pc2).
            const int64_t in_ms = 41440000, out_ms = in_ms + 31500;
            RallyState a = sector(0), b = sector(80);
            applySectorNext(a, 385, 385, pressTimeMs(in_ms, 0));
            applySectorNext(a, 565, 565, pressTimeMs(out_ms, 0));
            applySectorNext(b, 385, 385, pressTimeMs(in_ms, 80));
            applySectorNext(b, 565, 565, pressTimeMs(out_ms, 80));
            return a.last_pc_error.valid && b.last_pc_error.valid &&
                   std::fabs(a.last_pc_error.seconds - 0.5) < 1e-9 &&
                   std::fabs(a.last_pc_error.seconds - b.last_pc_error.seconds) < 1e-9 &&
                   b.trial_start_ms == out_ms - 80;
        });

        suite->addTest("offset does not touch the sector clock", []() {
            bool ok = true;
            for (int off : {-300, 300}) {
                RallyState r = sector(0);
                RallyState s = sector(off);
                s.trial_start_ms = 41440000; s.trial_start_m = 0.0;
                applyStartPressOffset(s, 10.0);
                applySectorNext(s, 385, 385, pressTimeMs(41440000, off));
                applySectorNext(r, 385, 385, 41440000);
                ok = ok && s.sector_start_ms == r.sector_start_ms &&
                     sectorClockZeroMs(s) == sectorClockZeroMs(r) && coInTimeMs(s) == coInTimeMs(r) &&
                     s.total_start_time_ms == r.total_start_time_ms;
            }
            return ok;
        });

        suite->addTest("offset is clamped and round-trips; an old config reads 0", []() {
            const char* path = "test_reaction_offset.json";
            RallyState a; a.reaction_offset_ms = 500;
            ConfigFile::save(a, path);
            RallyState b; ConfigFile::load(b, path);
            RallyState c; c.reaction_offset_ms = -120;
            ConfigFile::save(c, path);
            RallyState d; ConfigFile::load(d, path);
            // An old config: the same file without the key.
            std::istringstream in(dump(path));
            std::string line, old;
            bool had = false;
            while (std::getline(in, line)) {
                if (line.find("reaction_offset_ms") != std::string::npos) { had = true; continue; }
                old += line + "\n";
            }
            { std::ofstream o(path); o << old; }
            RallyState e; e.reaction_offset_ms = 0;
            ConfigFile::load(e, path);
            std::remove(path);
            return b.reaction_offset_ms == 300 && d.reaction_offset_ms == -120 && had &&
                   e.reaction_offset_ms == 0 && clampReactionOffsetMs(-900) == -300;
        });

        return suite;
    }
};

#endif
