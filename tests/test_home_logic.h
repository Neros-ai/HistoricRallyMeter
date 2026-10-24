#ifndef TEST_HOME_LOGIC_H
#define TEST_HOME_LOGIC_H

#include "test_framework.h"
#include "../calculations.h"
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

        // The Trip readout never shows below zero (adjustedDistanceMeters
        // clamps), and the history records what the readout showed.
        suite->addTest("trip history records what the readout showed, never below zero", []() {
            RallyState s;
            s.counters = false;
            s.calibration = 1000000;
            s.trip_start_cntr1 = 100;
            s.trip_distance_adjust_cm = -2000;   // -20 m correction
            resetTrip(s, 110, 0, 1);             // 10 m driven, 20 m back: reads 0
            return s.trip_history_m == "0";
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

        suite->addTest("history figures are grouped and keep a minus sign", []() {
            return formatDistanceGrouped(2140) == "2,140" && formatDistanceGrouped(-15) == "-15";
        });

        return suite;
    }
};

#endif // TEST_HOME_LOGIC_H
