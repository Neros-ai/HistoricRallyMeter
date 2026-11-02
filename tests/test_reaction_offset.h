#ifndef TEST_REACTION_OFFSET_H
#define TEST_REACTION_OFFSET_H

#include "test_framework.h"
#include "../reaction_offset.h"
#include "../surrey.h"
#include "../sector.h"
#include "../calculations.h"
#include "../config_file.h"
#include "../rally_state.h"
#include <cstdio>

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
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Reaction offset (RB-SET-12)");

        suite->addTest("reaction offset moves the pc clock start", []() {
            RallyState s; s.rally_type = 2; s.reaction_offset_ms = 80;
            s.trial_start_ms = 41440000;   // applySectorNext set it to the press
            shiftTubeStart(s, 0, 41440000);
            return s.trial_start_ms == 41439920;
        });

        suite->addTest("a next that starts no tube leaves trial_start_ms alone", []() {
            RallyState s; s.rally_type = 2; s.reaction_offset_ms = 80;
            s.trial_start_ms = 41000000;
            shiftTubeStart(s, 41000000, 41440000);
            return s.trial_start_ms == 41000000;
        });

        suite->addTest("offset zero changes nothing", []() {
            bool ok = true;
            for (int type : {0, 1, 2}) {
                RallyState s = started(type, 0, 5000000);
                s.trial_start_ms = 5000000; s.trial_start_m = 0.0;
                applyStartPressOffset(s, 10.0);
                applyArmPressOffset(s, 10.0);
                ok = ok && s.total_start_time_ms == 5000000 && s.segment_start_time_ms == 5000000 &&
                     s.trip_start_time_ms == 5000000 && s.trial_start_ms == 5000000 &&
                     s.total_distance_adjust_cm == 0 && s.sector_start_odometer_cm == 0;
            }
            RallyState t; t.rally_type = 2; t.trial_start_ms = 7;
            shiftTubeStart(t, 0, 7);
            return ok && t.trial_start_ms == 7 &&
                   pressTimeMs(1234, 0) == 1234 && pressBackCounts(10.0, 0, 1000000) == 0 &&
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
            const int64_t press = 36000000;
            return driven == 1004 && countsToMeters(driven, 1000000) == 1004.0 &&
                   segs[0].distance_m == 1004.0 && pressTimeMs(press, 100) == press - 100 &&
                   boundaryCountsAtPress(1005, v, 100, 1000000) == 1005 - 1;
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

        suite->addTest("italian start only moves a tube clock", []() {
            RallyState s = started(2, 100, 8000000);
            s.trial_start_ms = 8000000; s.trial_start_m = 0.0;
            applyStartPressOffset(s, 10.0);
            RallyState t = started(2, 100, 8000000);
            t.trial_start_ms = 8000000; t.trial_start_m = -1.0;
            applyStartPressOffset(t, 10.0);
            return s.trial_start_ms == 7999900 && s.total_start_time_ms == 8000000 &&
                   s.total_distance_adjust_cm == 0 && t.trial_start_ms == 8000000;
        });

        suite->addTest("arming an early departure moves only the distance", []() {
            RallyState s = started(1, 100, 8000000);
            applyArmPressOffset(s, 10.0);
            return s.total_start_time_ms == 8000000 && s.total_distance_adjust_cm == 100;
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

        suite->addTest("offset does not touch the sector clock", []() {
            bool ok = true;
            for (int off : {-300, 300}) {
                RallyState s = started(2, off, 41440000);
                s.sector_start_ms = 41400000;
                s.trial_start_ms = 41440000; s.trial_start_m = 0.0;
                const int64_t zero = sectorClockZeroMs(s);
                applyStartPressOffset(s, 10.0);
                shiftTubeStart(s, 0, 41440000 - off);
                ok = ok && s.sector_start_ms == 41400000 && sectorClockZeroMs(s) == zero;
            }
            return ok;
        });

        suite->addTest("offset is clamped and round-trips", []() {
            const char* path = "test_reaction_offset.json";
            RallyState a; a.reaction_offset_ms = 500;
            ConfigFile::save(a, path);
            RallyState b; ConfigFile::load(b, path);
            RallyState c; c.reaction_offset_ms = -120;
            ConfigFile::save(c, path);
            RallyState d; ConfigFile::load(d, path);
            RallyState e; e.reaction_offset_ms = 77;   // an old config without the key leaves the default
            RallyState f; ConfigFile::save(f, path);
            std::remove(path);
            return b.reaction_offset_ms == 300 && d.reaction_offset_ms == -120 &&
                   RallyState().reaction_offset_ms == 0 && clampReactionOffsetMs(-900) == -300;
        });

        return suite;
    }
};

#endif
