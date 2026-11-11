#ifndef TEST_SURREY_H
#define TEST_SURREY_H

#include "test_framework.h"
#include "../surrey.h"
#include "../sector.h"
#include "../calculations.h"
#include "../config_file.h"
#include "../rally_state.h"
#include <cstdio>
#include <fstream>

// RB-NAV-43: surrey is the third live rally type (0 greek, 1 surrey, 2 italian).
class TestSurrey {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Surrey Tests");

        suite->addTest("rally type surrey round-trips", []() {
            const char* path = "test_surrey_type.json";
            RallyState a; a.rally_type = 1;
            ConfigFile::save(a, path);
            RallyState b;
            ConfigFile::load(b, path);
            std::remove(path);
            return b.rally_type == 1 && isSurrey(b) && !isSurrey(RallyState());
        });

        suite->addTest("surrey defaults: metres, AB,AC", []() {
            RallyState s;
            applyRallyTypeDefaults(s, 2);
            s.measure_ab_bc = true;
            applyRallyTypeDefaults(s, 1);
            return s.rally_type == 1 && !s.distance_km && !s.measure_ab_bc;
        });

        suite->addTest("time taken round-trips; an old config gives -1", []() {
            const char* path = "test_surrey_taken.json";
            RallyState a; a.time_taken_ms = 560600;
            ConfigFile::save(a, path);
            RallyState b;
            ConfigFile::load(b, path);
            const bool ok = b.time_taken_ms == 560600;
            RallyState a2; a2.time_taken_ms = 560600; a2.time_taken_stage = 3;
            ConfigFile::save(a2, path);
            RallyState b2; ConfigFile::load(b2, path);
            if (b2.time_taken_stage != 3) { std::remove(path); return false; }
            { std::ofstream f(path); f << "{\n  \"rally_type\": 1\n}\n"; }
            RallyState c;
            ConfigFile::load(c, path);
            std::remove(path);
            return ok && c.time_taken_stage == 0 && c.time_taken_ms == -1 && c.surrey_finish_at_ms == 0;
        });

        suite->addTest("a finish press survives a restart: it is not offered again", []() {
            const char* path = "test_surrey_finish_saved.json";
            RallyState a;
            a.rally_type = 1;
            a.last_memory_slot = 1;
            a.stage_segments.resize(1);
            a.segment_current_number = 0;
            a.stage_complete = false;
            a.total_start_time_ms = 1000;
            if (!applySurreyFinish(a, 561600)) return false;
            ConfigFile::save(a, path);
            RallyState b;
            b.rally_type = 1;
            ConfigFile::load(b, path);
            std::remove(path);
            // What a restart restores of the finished stage, as main does from the saved file.
            b.last_memory_slot = 1;
            b.stage_segments.resize(1);
            b.segment_current_number = 0;
            b.stage_complete = true;
            return b.surrey_finish_at_ms == 561600 && b.time_taken_ms == 560600
                && !surreyFinishAvailable(b) && !applySurreyFinish(b, 900000)
                && b.time_taken_ms == 560600;
        });

        suite->addTest("stage start leaves navi total", []() {
            RallyState s; s.rally_type = 1; s.calibration = 1000000;
            s.odometer_start_cntr1 = s.odometer_start_cntr2 = 0;
            s.total_start_cntr1 = s.total_start_cntr2 = 9231;
            markSectorOdometer(s, 9231, 9231);
            const int64_t total = calculateDistanceCounts(s, 9231, 9231, s.total_start_cntr1, s.total_start_cntr2);
            return surreyNaviMetres(s, 9231, 9231) == 9231 && total == 0;
        });

        suite->addTest("set navi moves navi only", []() {
            RallyState s; s.rally_type = 1; s.calibration = 1000000;
            s.total_start_cntr1 = s.total_start_cntr2 = 22982;
            s.total_distance_adjust_mm = 7000;
            const bool ok = setOdometerMetres(s, 24040, 24040, 24024);
            return ok && surreyNaviMetres(s, 24040, 24040) == 24024 && s.total_distance_adjust_mm == 7000
                && calculateDistanceCounts(s, 24040, 24040, s.total_start_cntr1, s.total_start_cntr2) == 1058;
        });

        suite->addTest("greek set in surrey moves total only", []() {
            RallyState s; s.rally_type = 1; s.calibration = 1000000;
            s.odometer_adjust_mm = 550;
            s.total_start_cntr1 = s.total_start_cntr2 = 1000;
            // applyDistanceSet's arithmetic: solve total's correction for the wanted figure.
            const int64_t raw_mm = countsToMm(
                calculateDistanceCounts(s, 1800, 1800, s.total_start_cntr1, s.total_start_cntr2), s.calibration);
            s.total_distance_adjust_mm = static_cast<long>(8000000 - raw_mm);
            return s.odometer_adjust_mm == 550 && surreyNaviMetres(s, 1800, 1800) == 1800;
        });

        suite->addTest("surrey readout keys", []() {
            return std::string(readoutKeyName(1, 0)) == "total" && std::string(readoutKeyName(1, 1)) == "trip"
                && std::string(readoutKeyName(1, 2)) == "navi"
                && std::string(readoutKeyName(1, 3)) == "next\nspeed" && std::string(readoutKeyName(1, 4)) == "prev\nspeed"
                && std::string(readoutKeyName(0, 2)) == "countdown"
                && std::string(readoutKeyName(2, 2)) == "navi";
        });

        suite->addTest("surrey trip history is 4", []() {
            RallyState a; a.rally_type = 1;
            RallyState b;
            return tripHistoryRows(a) == 4 && tripHistoryRows(b) == 4;
        });

        suite->addTest("time taken is written m:ss.s, h:mm:ss.s from an hour", []() {
            return surreyTimeTakenText(560600) == "9:20.6" && surreyTimeTakenText(254600) == "4:14.6"
                && surreyTimeTakenText(560649) == "9:20.6" && surreyTimeTakenText(560650) == "9:20.7"
                && surreyTimeTakenText(3723400) == "1:02:03.4" && surreyTimeTakenText(0) == "0:00.0";
        });

        suite->addTest("surrey countdown reads metres and minutes at the speed", []() {
            return surreyCountdownTime(1912.0, 50.0) == "02:18" && surreyCountdownTime(100.0, 0.0) == "";
        });

        // Task 3: fixtures (calibration 1,000,000: one count = one metre).
        auto seg = [](double m, double kph, bool autoNext) {
            Segment g; g.distance_m = m; g.target_speed_kph = kph; g.autoNext = autoNext;
            return g;
        };
        auto ss1 = [seg]() { return std::vector<Segment>{seg(9418, 60, true)}; };
        auto ss2 = [seg]() { return std::vector<Segment>{seg(1717, 65, true), seg(2728, 50, true)}; };

        suite->addTest("SS1 press 83 m into box 11 fits row 1", [ss1]() {
            auto st = ss1();
            const bool ok = fitStageToStart(st, 9148.0, 9231.0, 1000000);
            // ideal time to the last "to" (18,566) at 60 kph
            return ok && st[0].distance_m == 9335.0
                && (st[0].distance_m / (60.0 / 3.6) > 560.0) && (st[0].distance_m / (60.0 / 3.6) < 560.2);
        });

        suite->addTest("SS2 late press keeps the auto change at box 30", [ss2]() {
            auto st = ss2();
            const bool ok = fitStageToStart(st, 22307.0, 23000.0, 1000000);
            return ok && st[0].distance_m == 1024.0 && st[1].distance_m == 2728.0 && st[0].autoNext;
        });

        suite->addTest("SS2 early press inside the slack", [ss2]() {
            auto st = ss2();
            return fitStageToStart(st, 22307.0, 22960.0, 1000000) && st[0].distance_m == 1064.0;
        });

        suite->addTest("total zeroed at the start: no fit", [ss2]() {
            RallyState s; s.rally_type = 1; s.calibration = 1000000;
            s.stage_segments = ss2();
            RallyState::MemorySlot slot; slot.first_from_m = 22307.0; slot.segments = ss2();
            s.sector_start_odometer_mm = 0;
            surreyStart(s, slot);
            return s.stage_segments[0].distance_m == 1717.0;
        });

        suite->addTest("press beyond row 1: no fit", [ss2]() {
            auto st = ss2();
            return !fitStageToStart(st, 22307.0, 24100.0, 1000000) && st[0].distance_m == 1717.0;
        });

        suite->addTest("memory slot untouched", [ss2]() {
            RallyState s; s.rally_type = 1; s.calibration = 1000000;
            s.segments = ss2(); s.stage_segments = ss2();
            s.time_taken_ms = 5000; s.time_taken_stage = 2; s.surrey_finish_at_ms = 7;
            RallyState::MemorySlot slot; slot.first_from_m = 22307.0; slot.segments = ss2();
            s.sector_start_odometer_mm = 23000000;
            surreyStart(s, slot);
            return s.stage_segments[0].distance_m == 1024.0
                && s.segments[0].distance_m == 1717.0 && slot.segments[0].distance_m == 1717.0
                && s.time_taken_ms == 5000 && s.time_taken_stage == 2 && s.surrey_finish_at_ms == 0;
        });

        // Task 4: next at the finish landmark, prev to undo (D3, D4).
        auto runningAt = [seg](std::vector<Segment> st, long cur) {
            RallyState s; s.rally_type = 1; s.calibration = 1000000;
            s.stage_segments = st; s.segment_current_number = cur; s.stage_complete = false;
            s.total_start_time_ms = 0;
            return s;
        };
        suite->addTest("next on the last row freezes time taken", [runningAt, ss1]() {
            RallyState s = runningAt(ss1(), 0); s.last_memory_slot = 1;
            const bool r = applySurreyFinish(s, 560600);
            return r && s.time_taken_ms == 560600 && s.time_taken_stage == 1 && s.stage_complete
                && s.surrey_finish_at_ms == 560600;
        });
        suite->addTest("next on a middle row is today's next", [runningAt, ss2]() {
            RallyState s = runningAt(ss2(), 0);
            return !applySurreyFinish(s, 100000) && s.time_taken_ms == -1 && !s.stage_complete
                && s.surrey_finish_at_ms == 0;
        });
        suite->addTest("second next at the finish does nothing", [runningAt, ss1]() {
            RallyState s = runningAt(ss1(), 0);
            applySurreyFinish(s, 560600);
            return !applySurreyFinish(s, 565000) && s.time_taken_ms == 560600;
        });
        suite->addTest("prev before the last to reopens", [runningAt, ss1]() {
            RallyState s = runningAt(ss1(), 0); s.last_memory_slot = 3;
            applySurreyFinish(s, 254600);
            const int64_t base = s.total_start_time_ms;
            const bool r = undoSurreyFinish(s, 53890.0, 53897.0);
            return r && s.time_taken_ms == -1 && s.time_taken_stage == 0 && !s.stage_complete
                && s.surrey_finish_at_ms == 0 && s.total_start_time_ms == base;
        });
        suite->addTest("prev past the last to does not", [runningAt, ss1]() {
            RallyState s = runningAt(ss1(), 0);
            applySurreyFinish(s, 254600);
            return !undoSurreyFinish(s, 53897.0, 53897.0) && s.stage_complete && s.time_taken_ms == 254600;
        });
        suite->addTest("prev with no press finish does nothing", [runningAt, ss1]() {
            RallyState s = runningAt(ss1(), 0);
            return !undoSurreyFinish(s, 100.0, 53897.0);
        });
        suite->addTest("no press: ends on distance as greek", [runningAt, ss1]() {
            RallyState s = runningAt(ss1(), 0); s.last_memory_slot = 1;
            surreyDistanceEnd(s, 561000);
            const bool a = s.time_taken_ms == 561000 && s.time_taken_stage == 1;
            RallyState t = runningAt(ss1(), 0);
            applySurreyFinish(t, 560600);
            surreyDistanceEnd(t, 561000);
            return a && t.time_taken_ms == 560600;
        });
        suite->addTest("a finish press after the roadbook distance ran out is taken, once", [runningAt, ss1]() {
            RallyState s = runningAt(ss1(), 0); s.last_memory_slot = 1;
            s.stage_complete = true;                    // as the distance end leaves it
            surreyDistanceEnd(s, 561000);
            const bool live = surreyFinishAvailable(s);
            const bool took = applySurreyFinish(s, 566200);     // the real finish, 5.2 s on
            const bool again = applySurreyFinish(s, 570000);    // a second press is not
            return live && took && !again && s.time_taken_ms == 566200 && s.time_taken_stage == 1
                && s.stage_complete && s.surrey_finish_at_ms == 566200;
        });
        suite->addTest("no late finish press once another stage is loaded or the stage is cleared", [runningAt, ss1]() {
            RallyState s = runningAt(ss1(), 0); s.last_memory_slot = 1;
            s.stage_complete = true;
            surreyDistanceEnd(s, 561000);
            RallyState other = s; other.last_memory_slot = 2;           // another stage loaded
            RallyState cleared = s; cleared.segment_current_number = -1; // abort / Reset Total
            RallyState greek = s; greek.rally_type = 0;
            return surreyFinishAvailable(s) && !surreyFinishAvailable(other)
                && !surreyFinishAvailable(cleared) && !surreyFinishAvailable(greek);
        });
        suite->addTest("greek ignores the surrey finish", [runningAt, ss1]() {
            RallyState s = runningAt(ss1(), 0); s.rally_type = 0;
            const bool a = applySurreyFinish(s, 1000);
            s.stage_complete = true; s.surrey_finish_at_ms = 5;
            surreyDistanceEnd(s, 2000);
            return !a && !undoSurreyFinish(s, 0.0, 100.0) && s.time_taken_ms == -1;
        });
        suite->addTest("key helpers: finish live on the last row only", [runningAt, ss1, ss2]() {
            RallyState a = runningAt(ss1(), 0), b = runningAt(ss2(), 0), c = runningAt(ss1(), 0);
            RallyState g = runningAt(ss1(), 0); g.rally_type = 0;
            c.stage_complete = true;
            RallyState d = runningAt(ss1(), 0); d.surrey_finish_at_ms = 5;
            return surreyFinishAvailable(a) && !surreyFinishAvailable(b) && !surreyFinishAvailable(g)
                && !surreyFinishAvailable(c) && !surreyFinishAvailable(d);
        });
        suite->addTest("key helpers: undo live before the last to only", [runningAt, ss1]() {
            RallyState s = runningAt(ss1(), 0);
            const bool none = surreyUndoAvailable(s, 1.0, 100.0);
            applySurreyFinish(s, 1000);
            const bool ok = surreyUndoAvailable(s, 99.0, 100.0) && !surreyUndoAvailable(s, 100.0, 100.0)
                && !surreyUndoAvailable(s, 101.0, 100.0);
            RallyState g = s; g.rally_type = 0;
            RallyState z = s; z.surrey_finish_at_ms = 0;
            return !none && ok && !surreyUndoAvailable(g, 99.0, 100.0) && !surreyUndoAvailable(z, 99.0, 100.0);
        });
        suite->addTest("undo refused after the slot changes or the stage is aborted", [runningAt, ss1]() {
            RallyState s = runningAt(ss1(), 0); s.last_memory_slot = 1;
            applySurreyFinish(s, 1000);
            const bool live = surreyUndoAvailable(s, 99.0, 100.0);
            RallyState recalled = s; recalled.last_memory_slot = 2;
            RallyState aborted = s; aborted.segment_current_number = -1;
            RallyState edited = s; edited.stage_segments.push_back(edited.stage_segments[0]);
            return live && !surreyUndoAvailable(recalled, 99.0, 100.0) && !surreyUndoAvailable(aborted, 99.0, 100.0)
                && !surreyUndoAvailable(edited, 99.0, 100.0) && !undoSurreyFinish(aborted, 99.0, 100.0)
                && aborted.time_taken_ms == 1000;
        });
        suite->addTest("surreyUndoAvailableNow: counters to navi to the slot's last to", [runningAt, ss1]() {
            RallyState s = runningAt(ss1(), 0); s.last_memory_slot = 1;
            s.memory_slots[0].first_from_m = 0.0;
            Segment g = s.stage_segments[0]; g.distance_m = 100.0;
            s.memory_slots[0].segments.assign(1, g);
            applySurreyFinish(s, 1000);
            setOdometerMetres(s, 0, 0, 0.0);
            setOdometerMetres(s, 0, 0, 50.0);
            const bool before = surreyUndoAvailableNow(s, 0, 0);
            RallyState p = s; setOdometerMetres(p, 0, 0, 150.0);
            RallyState q = s; q.last_memory_slot = 0;
            return before && !surreyUndoAvailableNow(p, 0, 0) && !surreyUndoAvailableNow(q, 0, 0);
        });
        suite->addTest("stage log time taken only for a stage that really ended", [runningAt, ss1]() {
            RallyState s = runningAt(ss1(), 0); s.last_memory_slot = 1;
            applySurreyFinish(s, 560600);
            const bool ended = surreyLogTimeTaken(s) == "9:20.6";
            RallyState aborted = s; aborted.segment_current_number = -1;
            RallyState other = s; other.last_memory_slot = 2;
            RallyState g = s; g.rally_type = 0;
            RallyState running = runningAt(ss1(), 0); running.last_memory_slot = 1; running.time_taken_ms = 5; running.time_taken_stage = 1;
            return ended && surreyLogTimeTaken(aborted) == "" && surreyLogTimeTaken(other) == ""
                && surreyLogTimeTaken(g) == "" && surreyLogTimeTaken(running) == "";
        });
        suite->addTest("time taken rounds to tenths", []() {
            return surreyTimeTakenText(59960) == "1:00.0" && surreyTimeTakenText(560640) == "9:20.6"
                && surreyTimeTakenText(3600000) == "1:00:00.0" && surreyTimeTakenText(-1) == "";
        });
        suite->addTest("time taken survives a power loss", [runningAt, ss1]() {
            RallyState s = runningAt(ss1(), 0);
            applySurreyFinish(s, 560600);
            restartDistancesAfterPowerLoss(s, 7, 7, 50);
            return s.time_taken_ms == 560600;
        });

        suite->addTest("surrey beeps measure from navi total", []() {
            RallyState s; s.rally_type = 1; s.calibration = 1000000;
            setOdometerMetres(s, 0, 0, 11060.0);
            RallyState g = s; g.rally_type = 0;
            return beepDistanceM(s, 0, 0, 1837.0) == 11060.0 && beepDistanceM(g, 0, 0, 1837.0) == 1837.0;
        });
        suite->addTest("surrey beeps count from the start box (waypoints are stage distances)", []() {
            RallyState s; s.rally_type = 1; s.calibration = 1000000;
            s.last_memory_slot = 1; s.memory_slots[0].first_from_m = 9148.0;
            setOdometerMetres(s, 0, 0, 11060.0);
            return beepDistanceM(s, 0, 0, 1837.0) == 11060.0 - 9148.0;
        });

        return suite;
    }
};

#endif
