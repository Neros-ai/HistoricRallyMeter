#ifndef TEST_STAGE_ROWS_H
#define TEST_STAGE_ROWS_H

#include "test_framework.h"
#include "../stage_rows.h"
#include "../calculations.h"
#include "../config_file.h"
#include "../rally_state.h"
#include <cmath>
#include <cstdio>

// RB-SEG-08: the Nero2.0 Segments table as data.
class TestStageRows {
    // 0 -> 1000 @30, 1000 -> 1500 carries, 1500 -> 2300 @50
    static std::vector<Segment> sample() {
        std::vector<Segment> v;
        appendStageDistances(v, 0, {1000, 1500, 2300});
        v[0].target_speed_kph = 30;
        setRowSpeed(v, 0, 30);
        setRowChange(v, 2, true);
        setRowSpeed(v, 2, 50);
        return v;
    }
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Stage Rows (RB-SEG-08)");

        suite->addTest("add distance makes one row per distance, each carrying the speed above", []() {
            std::vector<Segment> v;
            appendStageDistances(v, 0, {1000, 1500, 2300});
            return v.size() == 3 && v[0].distance_m == 1000 && v[1].distance_m == 500
                && v[2].distance_m == 800 && v[0].changesSpeed && !v[1].changesSpeed
                && !v[2].changesSpeed;
        });

        suite->addTest("rows: from, to, dist, number, chg", []() {
            auto rows = buildStageRows(sample(), 213, 0);
            return rows.size() == 3 && rows[0].number == 213 && rows[2].number == 215
                && rows[1].from_m == 1000 && rows[1].to_m == 1500 && rows[1].dist_m == 500
                && rows[0].chg && !rows[1].chg && rows[2].chg;
        });

        suite->addTest("m/s is whole: 30 kph is 8, 50 is 14, 60 is 17", []() {
            std::vector<Segment> v;
            appendStageDistances(v, 0, {100, 200, 300});
            setRowSpeed(v, 0, 30);
            setRowChange(v, 1, true); setRowSpeed(v, 1, 50);
            setRowChange(v, 2, true); setRowSpeed(v, 2, 60);
            auto r = buildStageRows(v, 1, 0);
            return r[0].ms == 8 && r[1].ms == 14 && r[2].ms == 17;
        });

        suite->addTest("a row that does not change speed follows the speed above", []() {
            auto v = sample();
            setRowSpeed(v, 0, 40);
            return v[1].target_speed_kph == 40 && v[2].target_speed_kph == 50;
        });

        suite->addTest("time is leg over speed and total is the running sum", []() {
            auto r = buildStageRows(sample(), 1, 0);
            // 1000 m at 30 kph = 120 s; 500 m at 30 = 60 s
            return std::abs(r[0].time_s - 120.0) < 0.01 && std::abs(r[1].time_s - 60.0) < 0.01
                && std::abs(r[1].total_s - 180.0) < 0.01
                && formatMmSs(r[0].time_s) == "2:00" && formatMmSs(r[2].total_s) == "3:58";
        });

        suite->addTest("auto: only where the speed changes, never on row 1, stored on the row above", []() {
            auto v = sample();
            setRowAuto(v, 2, false);
            auto r = buildStageRows(v, 1, 0);
            bool ok = !r[0].auto_available && !r[1].auto_available && r[2].auto_available
                   && !r[2].auto_on && !v[1].autoNext;
            setRowAuto(v, 1, false);   // not a change row: ignored
            return ok && v[0].autoNext;
        });

        suite->addTest("an unchanged row forces the row above to advance by itself", []() {
            auto v = sample();
            setRowAuto(v, 2, false);   // v[1].autoNext false
            setRowChange(v, 2, false); // row 3 now carries the speed above
            return v[1].autoNext;
        });

        suite->addTest("distances: thousands commas ignored, ; separates, whole metres", []() {
            auto p = parseStageDistances("1,000;1,500; 2,300", 0);
            return p.accepted.size() == 3 && p.accepted[0] == 1000 && p.accepted[2] == 2300
                && p.rest.empty();
        });

        suite->addTest("distances: stops at the first bad token and keeps the rest", []() {
            auto p = parseStageDistances("1000;900;2000", 0);
            return p.accepted.size() == 1 && p.rest == "900;2000";
        });

        suite->addTest("distances must pass the last row's end", []() {
            auto p = parseStageDistances("500;1200", 800);
            return p.accepted.empty() && p.rest == "500;1200";
        });

        suite->addTest("beep distances: every row's end", []() {
            auto w = deriveWaypoints(sample());
            return w.size() == 3 && w[0] == 1000 && w[1] == 1500 && w[2] == 2300;
        });

        suite->addTest("delete a middle row hands its length up; positions after it hold", []() {
            auto v = sample();
            double from = 0;
            deleteRow(v, from, 1);
            auto w = deriveWaypoints(v);
            return v.size() == 2 && w[0] == 1500 && w[1] == 2300 && from == 0;
        });

        suite->addTest("delete row 1 moves the starting distance", []() {
            auto v = sample();
            double from = 0;
            deleteRow(v, from, 0);
            auto r = buildStageRows(v, 1, from);
            return v.size() == 2 && from == 1000 && r[0].from_m == 1000 && r[0].chg
                && r[1].to_m == 2300;
        });

        suite->addTest("counts are recomputed from kph and metres", []() {
            auto v = sample();
            recomputeSegmentCounts(v, 600000);
            return std::abs(v[0].distance_counts - (1000.0 * 1e6) / 600000) < 1e-6
                && v[0].target_speed_counts_per_hour > 0;
        });

        suite->addTest("time of day: 10:45, 10:45:30, junk", []() {
            return parseTimeOfDay("10:45") == 38700 && parseTimeOfDay("10:45:30") == 38730
                && parseTimeOfDay("25:00") == -1 && parseTimeOfDay("abc") == -1
                && parseTimeOfDay("10:45:3x") == -1;
        });

        suite->addTest("metres: 6,200 and 6200 m", []() {
            return parseMetres("6,200") == 6200 && parseMetres("6200 m") == 6200
                && parseMetres("x") == -1 && parseMetres("") == -1;
        });

        suite->addTest("config keeps chg and each memory's number and first distance", []() {
            const char* path = "test_stage_rows_cfg.json";
            RallyState s;
            s.segments = sample();
            s.memory_slots[1].segments = sample();
            s.memory_slots[1].first_number = 213;
            s.memory_slots[1].first_from_m = 120.0;
            ConfigFile::save(s, path);
            RallyState t;
            ConfigFile::load(t, path);
            std::remove(path);
            return t.segments.size() == 3 && !t.segments[1].changesSpeed && t.segments[2].changesSpeed
                && t.memory_slots[1].first_number == 213 && t.memory_slots[1].first_from_m == 120.0
                && t.memory_slots[0].first_number == 1;
        });

        suite->addTest("editing a row's to m moves the next row's start; later ends hold", []() {
            auto v = sample();      // ends 1000, 1500, 2300
            bool ok = setRowTo(v, 0, 0, 900);
            auto w = deriveWaypoints(v);
            auto r = buildStageRows(v, 1, 0);
            return ok && w[0] == 900 && w[1] == 1500 && w[2] == 2300
                && r[1].from_m == 900 && r[1].dist_m == 600;
        });

        suite->addTest("to m is refused at or before the row's start, or at or past the next row's end", []() {
            auto v = sample();
            const bool a = setRowTo(v, 0, 1, 1000);    // not past its own start
            const bool b = setRowTo(v, 0, 1, 2300);    // reaches the next end
            const bool c = setRowTo(v, 0, 0, 1500);    // reaches the next end
            auto w = deriveWaypoints(v);
            return !a && !b && !c && w[0] == 1000 && w[1] == 1500 && w[2] == 2300;
        });

        suite->addTest("the last row's to m just moves the end of the stage", []() {
            auto v = sample();
            bool ok = setRowTo(v, 0, 2, 3000);
            auto w = deriveWaypoints(v);
            return ok && w[2] == 3000 && w[1] == 1500;
        });

        suite->addTest("deleting every row puts the stage's start back to 0", []() {
            auto v = sample();
            double from = 0;
            while (!v.empty()) deleteRow(v, from, 0);
            std::vector<Segment> w;
            appendStageDistances(w, from, {1250, 1500});
            return from == 0.0 && w.size() == 2;
        });

        suite->addTest("offset reads as minutes and hours from 60 s up", []() {
            return formatOffsetSeconds(2300) == "+2.3" && formatOffsetSeconds(-400) == "-0.4"
                && formatOffsetSeconds(-709200) == "-11:49.2" && formatOffsetSeconds(60000) == "+1:00.0"
                && formatOffsetSeconds(3723400) == "+1:02:03.4";
        });

        return suite;
    }
};

#endif // TEST_STAGE_ROWS_H
