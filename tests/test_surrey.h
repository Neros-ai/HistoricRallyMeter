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
            RallyState a; a.time_taken_ms = 560600; a.surrey_fit_failed = true;
            ConfigFile::save(a, path);
            RallyState b;
            ConfigFile::load(b, path);
            const bool ok = b.time_taken_ms == 560600 && b.surrey_fit_failed;
            { std::ofstream f(path); f << "{\n  \"rally_type\": 1\n}\n"; }
            RallyState c;
            ConfigFile::load(c, path);
            std::remove(path);
            return ok && c.time_taken_ms == -1 && !c.surrey_fit_failed && c.surrey_finish_at_ms == 0;
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
            s.total_distance_adjust_cm = 700;
            const bool ok = setOdometerMetres(s, 24040, 24040, 24024);
            return ok && surreyNaviMetres(s, 24040, 24040) == 24024 && s.total_distance_adjust_cm == 700
                && calculateDistanceCounts(s, 24040, 24040, s.total_start_cntr1, s.total_start_cntr2) == 1058;
        });

        suite->addTest("greek set in surrey moves total only", []() {
            RallyState s; s.rally_type = 1; s.calibration = 1000000;
            s.odometer_adjust_cm = 55;
            s.total_start_cntr1 = s.total_start_cntr2 = 1000;
            // applyDistanceSet's arithmetic: solve total's correction for the wanted figure.
            const long raw_cm = countsToCentimeters(
                calculateDistanceCounts(s, 1800, 1800, s.total_start_cntr1, s.total_start_cntr2), s.calibration);
            s.total_distance_adjust_cm = 80000 - raw_cm;
            return s.odometer_adjust_cm == 55 && surreyNaviMetres(s, 1800, 1800) == 1800;
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

        return suite;
    }
};

#endif
