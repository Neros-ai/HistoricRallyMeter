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

        suite->addTest("a stage taken for driving steps into ditto rows by itself; speed-change auto untouched", []() {
            // Stage 8 as stored: every auto off. 36 kph rows 0-2 (1, 2 ditto), 49 kph row 3, row 4 ditto.
            std::vector<Segment> v(5);
            v[0].target_speed_kph = 36; v[1].changesSpeed = false; v[2].changesSpeed = false;
            v[3].target_speed_kph = 49; v[4].changesSpeed = false;
            for (auto& seg : v) seg.autoNext = false;
            const std::vector<Segment> d = stageForDriving(v);
            return d.size() == 5
                && d[0].autoNext && d[1].autoNext      // into ditto rows 1 and 2
                && !d[2].autoNext                      // into the 49 kph change: the crew's auto (off)
                && d[3].autoNext                       // into ditto row 4
                && d[1].target_speed_kph == 36 && d[4].target_speed_kph == 49
                && !v[0].autoNext;                     // the stored roadbook is not touched
        });

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
            auto p = parseStageDistances("1,000;1,500; 2,300");
            return p.accepted.size() == 3 && p.accepted[0] == 1000 && p.accepted[2] == 2300
                && p.rest.empty();
        });

        suite->addTest("distances: stops at the first bad token and keeps the rest", []() {
            auto p = parseStageDistances("1000;abc;2000");
            return p.accepted.size() == 1 && p.rest == "abc;2000";
        });

        // RB-SEG-16: distances may fall anywhere, in any order; only 0 or less is a bad token.
        suite->addTest("distances may come in any order", []() {
            auto p = parseStageDistances("1000;900;2000");
            return p.accepted.size() == 3 && p.accepted[1] == 900 && p.rest.empty();
        });

        suite->addTest("a distance of 0 or less is a bad token", []() {
            auto p = parseStageDistances("500;0;700");
            return p.accepted.size() == 1 && p.rest == "0;700";
        });

        suite->addTest("km distances: ',' or '.' is the decimal, two decimals, metres out", []() {
            auto p = parseStageDistances("17,23;17.5", true);
            return p.accepted.size() == 2 && p.accepted[0] == 17230 && p.accepted[1] == 17500;
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

        // ---- RB-SEG-16: add distance splits a row ----
        suite->addTest("add 2000 inside 1,000-3,000 splits it; the new half carries the speed with chg off", []() {
            std::vector<Segment> v;
            appendStageDistances(v, 1000, {3000});
            setRowSpeed(v, 0, 50);
            std::string notice = insertStageDistances(v, 1000, {2000});
            auto r = buildStageRows(v, 381, 1000);
            return notice.empty() && r.size() == 2
                && r[0].from_m == 1000 && r[0].to_m == 2000 && r[0].kph == 50 && r[0].chg
                && r[1].from_m == 2000 && r[1].to_m == 3000 && r[1].kph == 50 && !r[1].chg
                && r[1].auto_on && r[1].number == 382;
        });

        suite->addTest("a distance on an existing point or at 0 is refused with a notice", []() {
            auto v = sample();
            const size_t n = v.size();
            std::string a = insertStageDistances(v, 0, {1500});
            std::string b = insertStageDistances(v, 0, {0});
            return !a.empty() && !b.empty() && v.size() == n;
        });

        suite->addTest("a list can mix inserts and appends in any order", []() {
            std::vector<Segment> v;
            appendStageDistances(v, 1000, {3000});
            std::string notice = insertStageDistances(v, 1000, {5000, 2000});
            auto r = buildStageRows(v, 1, 1000);
            return notice.empty() && r.size() == 3 && r[0].to_m == 2000 && r[1].to_m == 3000 && r[2].to_m == 5000;
        });

        suite->addTest("a distance before the stage's first from m is refused", []() {
            std::vector<Segment> v;
            appendStageDistances(v, 1000, {3000});
            std::string notice = insertStageDistances(v, 1000, {500});
            return !notice.empty() && v.size() == 1;
        });

        // ---- RB-SEG-17: box numbers ----
        suite->addTest("box defaults to the row above + 1; a typed box sticks and carries on", []() {
            auto v = sample();
            v[2].box = 250;
            auto r = buildStageRows(v, 100, 0);
            return r[0].number == 100 && r[1].number == 101 && r[2].number == 250;
        });

        suite->addTest("a stage saved before box existed numbers first_number + index", []() {
            const char* path = "test_stage_box_old.json";
            RallyState a;
            a.segments = sample();
            ConfigFile::save(a, path);
            RallyState b;
            ConfigFile::load(b, path);
            std::remove(path);
            auto r = buildStageRows(b.segments, 7, 0);
            return r[0].number == 7 && r[1].number == 8 && r[2].number == 9 && b.segments[1].box == -1;
        });

        suite->addTest("a typed box survives a save and load", []() {
            const char* path = "test_stage_box_rt.json";
            RallyState a;
            a.segments = sample();
            a.segments[2].box = 250;
            ConfigFile::save(a, path);
            RallyState b;
            ConfigFile::load(b, path);
            std::remove(path);
            return b.segments[2].box == 250 && b.segments[1].box == -1;
        });

        suite->addTest("typed distances: metres or kilometres", []() {
            return parseDistance("1,320", false) == 1320 && parseDistance("17,43", true) == 17430
                && parseDistance("17.5 km", true) == 17500 && parseDistance("x", true) == -1
                && parseDistance("", false) == -1;
        });

        suite->addTest("duration and elapsed read h:mm:ss", []() {
            return formatHMmSs(131) == "0:02:11" && formatHMmSs(35999) == "9:59:59" && formatHMmSs(0) == "0:00:00"
                && formatHMmSs(3661) == "1:01:01";
        });

        suite->addTest("distance text: metres grouped, km two decimals", []() {
            return formatDistance(1320, false) == "1,320" && formatDistance(540, false) == "540"
                && formatDistance(17430, true) == "17.43" && formatDistance(380, true) == "0.38"
                && formatDistance(125, true) == "0.13";
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
