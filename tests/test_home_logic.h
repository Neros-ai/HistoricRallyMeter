#ifndef TEST_HOME_LOGIC_H
#define TEST_HOME_LOGIC_H

#include "test_framework.h"
#include "../calculations.h"
#include "../home_calc.h"
#include "../freeze.h"
#include "../config_file.h"
#include "../counter_poller.h"
#include "../rally_state.h"
#include "mock_i2c_counter.h"
#include <chrono>
#include <cstdio>
#include <thread>
#include <fstream>
#include <sstream>

// RB-NAV-20: the logic behind the Nero2.0 Home page -- trip history, reverse,
// the speed-change line, Stage arrival and the per-memory start fields.
class TestHomeLogic {
    // Drives the poller with raw counts by hand through two mock counters.
    static void feed(CounterPoller& p, MockI2CCounter& a, MockI2CCounter& b, uint32_t v) {
        a.setRegisterValue(0x07, v);
        b.setRegisterValue(0x07, v);
        // The poller will not read twice inside 5 ms.
        std::this_thread::sleep_for(std::chrono::milliseconds(7));
        p.poll(&a, &b, 0x07);
    }
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Home Logic (RB-NAV-20)");

        suite->addTest("trip history: newest first, capped at six", []() {
            std::string h;
            for (long m : {1, 2, 3, 4, 5, 6, 7}) h = pushTripHistory(h, m);
            return h == "7,6,5,4,3,2";
        });

        suite->addTest("trip history: parse empty, two, junk, negative", []() {
            return parseTripHistory("").empty()
                && parseTripHistory("1234,56").size() == 2
                && parseTripHistory("1,x,3").size() == 2
                && parseTripHistory("-15,7")[0] == -15;
        });

        suite->addTest("trip reset records the trip as it read, then zeroes it", []() {
            RallyState s;
            s.counters = false;
            s.calibration = 1000000;   // 1 m per count
            s.trip_start_cntr1 = 100;
            resetTrip(s, 1100, 0, 5000);
            return s.trip_history_m == "1000"
                && s.trip_start_cntr1 == 1100 && s.trip_start_time_ms == 5000;
        });

        // The history records what the readout showed, a negative one included
        // (reversing past the start reads below zero).
        suite->addTest("trip history records what the readout showed, a negative one with its sign", []() {
            RallyState s;
            s.counters = false;
            s.calibration = 1000000;
            s.trip_start_cntr1 = 100;
            s.trip_distance_adjust_cm = -2000;   // -20 m correction
            resetTrip(s, 110, 0, 1);             // 10 m driven, 20 m back: reads -10
            return s.trip_history_m == "-10";
        });

        suite->addTest("config round-trips trip history, reverse shift and slot starts", []() {
            const char* path = "test_home_logic_cfg.json";
            RallyState s;
            s.trip_history_m = "-15,740,2140";
            s.reverse_shift1 = 120;
            s.reverse_shift2 = -4;
            s.memory_slots[2].start_time_s = 38700;
            s.memory_slots[2].start_distance_m = 6200;
            ConfigFile::save(s, path);
            RallyState t;
            ConfigFile::load(t, path);
            std::remove(path);
            return t.trip_history_m == "-15,740,2140"
                && t.reverse_shift1 == 120 && t.reverse_shift2 == -4
                && t.memory_slots[2].start_time_s == 38700
                && t.memory_slots[2].start_distance_m == 6200
                && t.memory_slots[0].start_time_s == -1;
        });

        suite->addTest("reversing past the start displays negative metres and counts back up", [this]() {
            RallyState s;
            s.counters = false;
            s.calibration = 1000000;   // 1 m per count
            CounterPoller p;
            MockI2CCounter a(1, 0x70), b(1, 0x71);
            feed(p, a, b, 1000);
            s.total_start_cntr1 = p.getMostRecent().cntr1;
            feed(p, a, b, 1005);               // 5 m forward
            p.setReverse(true);
            feed(p, a, b, 1017);               // 12 m back: -7 m
            auto now = p.getMostRecent();
            long m = adjustedDistanceMeters(countsToCentimeters(
                calculateDistanceCounts(s, now.cntr1, now.cntr2, s.total_start_cntr1, s.total_start_cntr2),
                s.calibration), 0);
            bool neg = (m == -7);
            p.setReverse(false);
            feed(p, a, b, 1024);               // 7 m forward: back to 0
            now = p.getMostRecent();
            long m2 = adjustedDistanceMeters(countsToCentimeters(
                calculateDistanceCounts(s, now.cntr1, now.cntr2, s.total_start_cntr1, s.total_start_cntr2),
                s.calibration), 0);
            return neg && m2 == 0;
        });

        suite->addTest("poller: forward, virtual count equals the raw count", [this]() {
            CounterPoller p;
            MockI2CCounter a(1, 0x70), b(1, 0x71);
            feed(p, a, b, 1000);
            feed(p, a, b, 1050);
            return p.getMostRecent().cntr1 == 1050 && p.getMostRecentRaw().cntr1 == 1050;
        });

        suite->addTest("poller: reverse subtracts, forward resumes from the lowered value", [this]() {
            CounterPoller p;
            MockI2CCounter a(1, 0x70), b(1, 0x71);
            feed(p, a, b, 1000);
            feed(p, a, b, 1100);                 // +100 forward -> 1100
            p.setReverse(true);
            feed(p, a, b, 1150);                 // raw +50 -> virtual -50
            bool down = p.getMostRecent().cntr1 == 1050 && p.getMostRecentRaw().cntr1 == 1150;
            p.setReverse(false);
            feed(p, a, b, 1200);                 // raw +50 -> virtual +50
            return down && p.getMostRecent().cntr1 == 1100;
        });

        suite->addTest("poller: hold ignores travel, release resumes from the held count", [this]() {
            CounterPoller p;
            MockI2CCounter a(1, 0x70), b(1, 0x71);
            feed(p, a, b, 1000);
            feed(p, a, b, 1500);
            p.setHold(true);
            for (int raw = 2500; raw <= 6500; raw += 1000) feed(p, a, b, raw);   // 5000 counts off route
            const bool held = p.getMostRecent().cntr1 == 1500 && p.getMostRecentRaw().cntr1 == 6500 && p.hold();
            p.setHold(false);
            feed(p, a, b, 6600);
            return held && p.getMostRecent().cntr1 == 1600;
        });

        suite->addTest("poller: hold beats reverse; reverse subtracts after release", [this]() {
            CounterPoller p;
            MockI2CCounter a(1, 0x70), b(1, 0x71);
            feed(p, a, b, 1000);
            feed(p, a, b, 1500);
            p.setReverse(true);
            p.setHold(true);
            feed(p, a, b, 1800);
            const bool held = p.getMostRecent().cntr1 == 1500;
            p.setHold(false);
            feed(p, a, b, 1900);
            return held && p.getMostRecent().cntr1 == 1400;
        });

        suite->addTest("poller: a held restart starts on the held count, not raw minus the old shift", [this]() {
            CounterPoller p;
            MockI2CCounter a(1, 0x70), b(1, 0x71);
            p.setVirtualShift(-4000, -4000);   // would give 13000 at raw 9000
            p.setHold(true);
            p.setHeldVirtual(1500, 1500);
            feed(p, a, b, 9000);
            const bool first = p.getMostRecent().cntr1 == 1500 && p.getMostRecent().cntr2 == 1500;
            feed(p, a, b, 9100);
            return first && p.getMostRecent().cntr1 == 1500 && p.virtual1() == 1500;
        });

        suite->addTest("freeze holds Total and the segment; release carries on from the held figure", [this]() {
            RallyState s;
            s.counters = false;
            s.calibration = 1000000;
            CounterPoller p;
            p.setSpuriousCheckEnabled(false);   // whole-km steps in one poll
            MockI2CCounter a(1, 0x70), b(1, 0x71);
            feed(p, a, b, 0);
            s.total_start_cntr1 = s.segment_start_cntr1 = p.getMostRecent().cntr1;
            feed(p, a, b, 2000);
            applyFreeze(s, p, true);
            feed(p, a, b, 4500);
            feed(p, a, b, 7000);
            auto now = p.getMostRecent();
            const bool held = calculateDistanceCounts(s, now.cntr1, now.cntr2, s.total_start_cntr1, s.total_start_cntr2) == 2000
                && calculateDistanceCounts(s, now.cntr1, now.cntr2, s.segment_start_cntr1, s.segment_start_cntr2) == 2000
                && s.freeze_on && s.freeze_virt1 == 2000;
            applyFreeze(s, p, false);
            feed(p, a, b, 7100);
            now = p.getMostRecent();
            return held && !s.freeze_on
                && calculateDistanceCounts(s, now.cntr1, now.cntr2, s.total_start_cntr1, s.total_start_cntr2) == 2100
                && s.reverse_shift1 == p.virtualShift1();
        });

        suite->addTest("freeze state and held count survive a config save and load", []() {
            const char* path = "test_freeze_cfg.json";
            RallyState s;
            s.freeze_on = true;
            s.freeze_virt1 = 1500;
            s.freeze_virt2 = 1498;
            ConfigFile::save(s, path);
            RallyState t;
            ConfigFile::load(t, path);
            std::remove(path);
            RallyState u;
            return t.freeze_on && t.freeze_virt1 == 1500 && t.freeze_virt2 == 1498 && !u.freeze_on;
        });

        suite->addTest("restart while frozen reads the held figure", [this]() {
            RallyState s;
            s.freeze_on = true;
            s.freeze_virt1 = s.freeze_virt2 = 1500;
            s.reverse_shift1 = s.reverse_shift2 = -4000;
            CounterPoller p;
            p.setVirtualShift(s.reverse_shift1, s.reverse_shift2);
            restoreFreeze(s, p);
            MockI2CCounter a(1, 0x70), b(1, 0x71);
            feed(p, a, b, 9000);                 // the car moved while the Pi was down
            const bool first = p.getMostRecent().cntr1 == 1500 && p.hold();
            RallyState idle;                     // not frozen: the ordinary shift applies
            CounterPoller q;
            q.setVirtualShift(-4000, -4000);
            restoreFreeze(idle, q);
            feed(q, a, b, 9000);
            return first && q.getMostRecent().cntr1 == 13000 && !q.hold();
        });

        suite->addTest("a counter power loss ends freeze", []() {
            RallyState s;
            s.freeze_on = true;
            s.freeze_virt1 = s.freeze_virt2 = 1500;
            restartDistancesAfterPowerLoss(s, 10, 10, 1000);
            return !s.freeze_on && s.freeze_virt1 == 0 && s.freeze_virt2 == 0;
        });

        suite->addTest("next speed while frozen sets the boundary at the held distance", [this]() {
            RallyState s;
            s.counters = false;
            s.calibration = 1000000;
            std::vector<Segment> v(2);
            v[0].distance_counts = 5000; v[0].distance_m = 5000;
            v[1].distance_counts = 3000; v[1].distance_m = 3000;
            CounterPoller p;
            p.setSpuriousCheckEnabled(false);   // whole-km steps in one poll
            MockI2CCounter a(1, 0x70), b(1, 0x71);
            feed(p, a, b, 0);
            s.segment_start_cntr1 = p.getMostRecent().cntr1;
            feed(p, a, b, 4200);
            applyFreeze(s, p, true);
            feed(p, a, b, 6000);                  // off route, not counted
            auto now = p.getMostRecent();
            const int64_t driven = calculateDistanceCounts(s, now.cntr1, now.cntr2, s.segment_start_cntr1, s.segment_start_cntr2);
            retimeSegmentBoundaryForward(v, 0, driven, s.calibration);
            return driven == 4200 && v[0].distance_counts == 4200 && v[1].distance_counts == 3800;
        });

        suite->addTest("poller: the shift is saved and restored", [this]() {
            CounterPoller p;
            MockI2CCounter a(1, 0x70), b(1, 0x71);
            feed(p, a, b, 1000);
            p.setReverse(true);
            feed(p, a, b, 1100);                 // virtual 900, shift 200
            int64_t shift = p.virtualShift1();
            CounterPoller q;
            q.setVirtualShift(shift, shift);
            feed(q, a, b, 1100);                 // restarted: same raw reading
            return shift == 200 && q.getMostRecent().cntr1 == 900;
        });

        suite->addTest("Total distance falls while reversed", [this]() {
            RallyState s;
            s.counters = false;
            s.calibration = 1000000;
            CounterPoller p;
            MockI2CCounter a(1, 0x70), b(1, 0x71);
            feed(p, a, b, 1000);
            s.total_start_cntr1 = p.getMostRecent().cntr1;
            feed(p, a, b, 1100);
            p.setReverse(true);
            feed(p, a, b, 1130);
            auto now = p.getMostRecent();
            return calculateDistanceCounts(s, now.cntr1, now.cntr2,
                                           s.total_start_cntr1, s.total_start_cntr2) == 70;
        });

        suite->addTest("speed change line", []() {
            return speedChangeLine(50, 60, true, false) == "50<>60 kph"
                && speedChangeLine(50, 60, false, false).empty()
                && speedChangeLine(80, 100, true, true) == "50<>62 mph";
        });

        suite->addTest("stage arrival: 6,200 m with 14 min to go is 29 kph", []() {
            StageArrival a = stageArrival(6200, 14);
            return a.valid && a.minutes == 14 && a.speed_kph == 29;
        });

        suite->addTest("stage arrival minutes count down in whole minutes, rounded down", []() {
            return stageArrival(6200, 14.75).minutes == 14      // 14 min 45 s
                && stageArrival(6200, 14.0).minutes == 14
                && stageArrival(6200, 13.99).minutes == 13
                && stageArrival(6200, 14.75).speed_kph == 27;   // 6.2 km in 13.75 min
        });

        suite->addTest("stage arrival is blank with a minute or less, or no distance", []() {
            return !stageArrival(6200, 1).valid && !stageArrival(6200, 0.5).valid
                && !stageArrival(0, 14).valid;
        });

        // RB-NAV-46: greek Home, final
        suite->addTest("Home shows the newest four trip readings, newest first", []() {
            RallyState s;
            for (long m : {100L, 200L, 300L, 400L, 500L, 600L}) s.trip_history_m = pushTripHistory(s.trip_history_m, m);
            std::vector<long> h = homeTripHistory(s);
            return h.size() == 4 && h[0] == 600 && h[3] == 300;
        });

        suite->addTest("Home history with fewer than four readings shows what there is", []() {
            RallyState s;
            s.trip_history_m = pushTripHistory(s.trip_history_m, 2140);
            return homeTripHistory(s).size() == 1 && homeTripHistory(RallyState()).empty();
        });

        suite->addTest("time to the next speed change runs through rows that carry the speed on", []() {
            // 36 kph = 10 m/s. Row 0: 600 m left -> 60 s; row 1 carries, 1000 m -> 100 s; row 2 changes.
            std::vector<Segment> v(3);
            v[0].target_speed_kph = 36; v[0].distance_m = 3000;
            v[1].target_speed_kph = 36; v[1].distance_m = 1000; v[1].changesSpeed = false;
            v[2].target_speed_kph = 54; v[2].distance_m = 2000;
            bool ok = false;
            const double s = nextChangeSeconds(v, 0, 600.0, &ok);
            return ok && std::fabs(s - 160.0) < 0.001;
        });

        suite->addTest("time to the next speed change counts an overrun of the row against the carried rows", []() {
            // Still on row 0 (ends 540 m) at 720 m: 180 m over, 1470 m of carried rows -> 1290 m, 129 s at 36 kph.
            std::vector<Segment> v(3);
            v[0].target_speed_kph = 36; v[0].distance_m = 540;
            v[1].target_speed_kph = 36; v[1].distance_m = 1470; v[1].changesSpeed = false;
            v[2].target_speed_kph = 49; v[2].distance_m = 1640;
            bool ok = false;
            const double s = nextChangeSeconds(v, 0, -180.0, &ok);
            return ok && std::fabs(s - 129.0) < 0.001 && metresToNextChange(v, 0, -180) == 1290;
        });

        suite->addTest("next speed folds the carried rows in, so the next row is the speed change", []() {
            // Manual's stage 8: 36 kph 540 + 780 + 690 (ditto), 49 kph at 2010 m.
            std::vector<Segment> v(4);
            v[0].target_speed_kph = 36; v[0].distance_m = 540; v[0].distance_counts = 5400;
            v[1].target_speed_kph = 36; v[1].distance_m = 780; v[1].distance_counts = 7800; v[1].changesSpeed = false;
            v[2].target_speed_kph = 36; v[2].distance_m = 690; v[2].distance_counts = 6900; v[2].changesSpeed = false;
            v[3].target_speed_kph = 49; v[3].distance_m = 860; v[3].distance_counts = 8600;
            const long folded = foldCarriedRows(v, 0);
            const bool ok = folded == 2 && v.size() == 2 && v[0].distance_m == 2010 && v[0].distance_counts == 20100
                && v[1].target_speed_kph == 49 && segmentStartStageCounts(v, 1) == 20100;
            // Nothing to fold: next row already changes, or no change follows.
            const bool none = foldCarriedRows(v, 0) == 0 && foldCarriedRows(v, 1) == 0;
            return ok && none;
        });

        suite->addTest("countdown metres run to the next speed change, not the end of the row", []() {
            // The manual's stage 8: 36 kph rows to 540 m and on to 2010 m, change at 2010 m.
            std::vector<Segment> v(3);
            v[0].target_speed_kph = 36; v[0].distance_m = 540;
            v[1].target_speed_kph = 36; v[1].distance_m = 1470; v[1].changesSpeed = false;
            v[2].target_speed_kph = 49; v[2].distance_m = 1640;
            // 245 m in: 295 m left in row 0, 1765 m to the change, which is row 2.
            const bool carried = metresToNextChange(v, 0, 295) == 1765 && nextChangeIndex(v, 0) == 2;
            // Every row a change: the countdown is just the row's own remainder.
            v[1].changesSpeed = true;
            const bool plain = metresToNextChange(v, 0, 295) == 295 && nextChangeIndex(v, 0) == 1;
            // No later change: runs to the end of the stage. Over-run stays negative.
            v[2].changesSpeed = false;
            const bool to_end = metresToNextChange(v, 1, 100) == 1740 && nextChangeIndex(v, 1) == -1;
            const bool over = metresToNextChange(v, 2, -20) == -20;
            return carried && plain && to_end && over;
        });

        suite->addTest("time to the next speed change: none after the last change, none at 0 kph", []() {
            std::vector<Segment> v(2);
            v[0].target_speed_kph = 36; v[0].distance_m = 1000;
            v[1].target_speed_kph = 36; v[1].distance_m = 1000; v[1].changesSpeed = false;
            bool ok = true;
            nextChangeSeconds(v, 0, 500.0, &ok);
            const bool none_after = !ok;
            v[1].changesSpeed = true; v[0].target_speed_kph = 0;
            nextChangeSeconds(v, 0, 500.0, &ok);
            const bool none_stopped = !ok;
            nextChangeSeconds(v, 7, 500.0, &ok);
            return none_after && none_stopped && !ok;
        });

        suite->addTest("driver speeds read to two decimals in italian and one in greek", []() {
            return driverSpeedText(24.26, 2) == "24.26" && driverSpeedText(24.26, 0) == "24.3" && driverSpeedText(30.0, 0) == "30.0";
        });

        suite->addTest("countdown reads mm:ss with two figures each", []() {
            return formatClockMmSs(131) == "02:11" && formatClockMmSs(0) == "00:00" && formatClockMmSs(3725) == "62:05";
        });

        suite->addTest("A-B, B-C zero key shows only on a greek page with measure type AB,BC, and not single-display", []() {
            RallyState s;
            const bool off_by_default = !showAdjZeroKey(s, false);
            s.measure_ab_bc = true;
            const bool greek_on = showAdjZeroKey(s, false);
            const bool single_off = !showAdjZeroKey(s, true);
            s.rally_type = 2;
            return off_by_default && greek_on && single_off && !showAdjZeroKey(s, false);
        });

        suite->addTest("history figures are grouped and keep a minus sign", []() {
            return formatDistanceGrouped(2140) == "2,140" && formatDistanceGrouped(-15) == "-15";
        });

        return suite;
    }
};

#endif // TEST_HOME_LOGIC_H
