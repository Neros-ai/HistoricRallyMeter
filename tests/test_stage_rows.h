#ifndef TEST_STAGE_ROWS_H
#define TEST_STAGE_ROWS_H

#include "test_framework.h"
#include "../stage_rows.h"
#include "../sector.h"
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

        // Owner, 2026-11-15: in greek and surrey a new speed change defaults to automatic (first row has none).
        suite->addTest("mile conversions use the exact international mile", []() {
            if (METRES_PER_MILE != 1609.344 || std::fabs(KM_PER_MILE - 1.609344) > 1e-12) return false;
            if (std::fabs(kphToMph(1.609344) - 1.0) > 1e-12 || std::fabs(mphToKph(1.0) - 1.609344) > 1e-12) return false;
            if (std::fabs(kphToMph(mphToKph(37.5)) - 37.5) > 1e-9) return false;   // round trip
            // 100 km/h is 62.1371192... mph; the old 0.621371 constant gave 62.1371 (3e-7 low)
            return std::fabs(kphToMph(100.0) - 62.13711922) < 1e-6 && std::fabs(MILES_PER_KM * KM_PER_MILE - 1.0) < 1e-15;
        });

        suite->addTest("a row made a speed change defaults to automatic, and can be unticked", []() {
            std::vector<Segment> v;
            appendStageDistances(v, 0, {1000, 1500, 2300});
            setRowSpeed(v, 0, 30);
            v[1].autoNext = false;   // e.g. an uploaded stage that left it off
            setRowChange(v, 2, true, true);
            setRowSpeed(v, 2, 50);
            auto on = buildStageRows(v, 1, 0);
            if (!on[2].auto_available || !on[2].auto_on) return false;
            setRowAuto(v, 2, false);
            return !buildStageRows(v, 1, 0)[2].auto_on;
        });

        // Owner, 2026-11-15: the roadbook ends win. Setting the start afterwards only shortens the first row.
        suite->addTest("setting the start after typing the ends keeps every end", []() {
            std::vector<Segment> v;
            appendStageDistances(v, 0, {520, 1310, 2450});
            double from = 0;
            if (!setFirstFrom(v, from, 100)) return false;
            auto rows = buildStageRows(v, 1, from);
            return from == 100 && rows.size() == 3 && rows[0].dist_m == 420
                && rows[0].to_m == 520 && rows[1].to_m == 1310 && rows[2].to_m == 2450;
        });

        suite->addTest("a start at or beyond the first end is refused and changes nothing", []() {
            std::vector<Segment> v;
            appendStageDistances(v, 0, {520, 1310});
            double from = 0;
            return !setFirstFrom(v, from, 520) && !setFirstFrom(v, from, 600)
                && from == 0 && v[0].distance_m == 520 && v[1].distance_m == 790;
        });

        suite->addTest("a start set before the first end is typed is kept, and the first row runs from it", []() {
            std::vector<Segment> v;
            double from = 0;
            if (!setFirstFrom(v, from, 100)) return false;
            appendStageDistances(v, from, {520, 1310});
            return from == 100 && v[0].distance_m == 420 && v[1].distance_m == 790;
        });

        // Owner, 2026-11-15: miles are stored to the nearest 1/1000 mile and shown cut down.
        suite->addTest("a typed mile figure is stored as exactly that many thousandths of a mile", []() {
            auto p = parseStageDistances("1.234;0.0004", DIST_MI);
            // 0.0004 mi is under half a thousandth: a distance that rounds to 0 is not a distance
            return p.accepted.size() == 1 && std::fabs(p.accepted[0] - 1.234 * 1609.344) < 1e-9;
        });

        suite->addTest("a mile figure never reads ahead of the car by more than a millimetre", []() {
            const double typed = 1.234 * 1609.344;
            return formatDistance(typed, DIST_MI) == "1.234"
                && formatDistance(typed - 0.4, DIST_MI) == "1.233"
                && formatDistance(typed + 0.4, DIST_MI) == "1.234";
        });

        suite->addTest("the add distance key and its example name the unit in use", []() {
            return distUnitKeyText(DIST_KM) == "km 0.00" && distUnitKeyText(DIST_KM3) == "km 0.000"
                && distUnitKeyText(DIST_M) == "m" && distUnitKeyText(DIST_MI2) == "mi 0.00"
                && distUnitKeyText(DIST_MI) == "mi 0.000"
                && addDistanceKeyText(DIST_KM) == "add distance\nkm 0.00" && addDistanceKeyText(DIST_KM3) == "add distance\nkm 0.000"
                && addDistanceKeyText(DIST_M) == "add distance\nm" && addDistanceKeyText(DIST_MI2) == "add distance\nmi 0.00"
                && addDistanceKeyText(DIST_MI) == "add distance\nmi 0.000"
                && distAddExample(DIST_KM) == "1.00;1.50;2.30" && distAddExample(DIST_KM3) == "1.000;1.500;2.300"
                && distAddExample(DIST_M) == "1000;1500;2300" && distAddExample(DIST_MI2) == "1.00;1.50;2.30"
                && distAddExample(DIST_MI) == "1.000;1.500;2.300";
        });

        // Owner, 2026-11-15: km -> miles keeps the figures and changes the unit.
        suite->addTest("km to miles keeps the figures: a 5 km stage becomes a 5 mile stage, and back again", []() {
            std::vector<Segment> v;
            appendStageDistances(v, 1000, {5000, 12345});
            double from = 1000;
            long start = 2000;
            if (!reinterpretStageForUnit(v, from, start, DIST_KM3, DIST_MI)) return false;
            auto rows = buildStageRows(v, 1, from);
            if (std::fabs(from - 1.0 * 1609.344) > 1e-9 || start != std::lround(2.0 * 1609.344)) return false;
            if (std::fabs(rows[0].to_m - 5.0 * 1609.344) > 1e-9 || std::fabs(rows[1].to_m - 12.345 * 1609.344) > 1e-9) return false;
            if (formatDistance(rows[0].to_m, DIST_MI) != "5.000" || formatDistance(rows[1].to_m, DIST_MI) != "12.345") return false;
            if (!reinterpretStageForUnit(v, from, start, DIST_MI, DIST_KM3)) return false;
            auto back = buildStageRows(v, 1, from);
            return from == 1000 && start == 2000 && back[0].to_m == 5000 && back[1].to_m == 12345
                && back[0].dist_m == 4000 && back[1].dist_m == 7345;
        });

        // Owner, 2026-11-15: "5000 always stays 5000" -- metres to miles is 5.000 miles, and back to metres 5000.
        suite->addTest("metres to miles keeps the figures: 5000 m becomes 5.000 miles, and back again", []() {
            std::vector<Segment> v;
            appendStageDistances(v, 0, {5000, 12345});
            double from = 0; long start = 2000;
            if (!reinterpretStageForUnit(v, from, start, DIST_M, DIST_MI)) return false;
            auto rows = buildStageRows(v, 1, from);
            if (formatDistance(rows[0].to_m, DIST_MI) != "5.000" || formatDistance(rows[1].to_m, DIST_MI) != "12.345") return false;
            if (!reinterpretStageForUnit(v, from, start, DIST_MI, DIST_M)) return false;
            auto back = buildStageRows(v, 1, from);
            return start == 2000 && back[0].to_m == 5000 && back[1].to_m == 12345;
        });

        suite->addTest("a change of format inside km or metres touches nothing", []() {
            std::vector<Segment> v;
            appendStageDistances(v, 0, {5000, 12345});
            double from = 0; long start = 2000;
            return !reinterpretStageForUnit(v, from, start, DIST_KM, DIST_KM3)
                && !reinterpretStageForUnit(v, from, start, DIST_KM3, DIST_M)
                && !reinterpretStageForUnit(v, from, start, DIST_MI, DIST_MI2)
                && v[0].distance_m == 5000 && v[1].distance_m == 7345 && start == 2000;
        });

        suite->addTest("set entries take 7 characters when the unit shows 3 decimals, else 6", []() {
            return distEntryMaxChars(DIST_KM3) == 7 && distEntryMaxChars(DIST_MI) == 7
                && distEntryMaxChars(DIST_KM) == 6 && distEntryMaxChars(DIST_MI2) == 6 && distEntryMaxChars(DIST_M) == 6;
        });

        // Owner, 2026-11-15: a calculated speed is kept in full but shown to two decimals, wherever it is shown.
        suite->addTest("a greek or surrey segment speed is shown to two decimals, never the full calculated figure", []() {
            Segment seg;
            StageRow row;
            row.kph = 32.7533; row.chg = true;
            const SegRowText g = segRowText(seg, row, 0, RALLY_GREEK, DIST_KM3, false);
            const SegRowText m = segRowText(seg, row, 0, RALLY_SURREY, DIST_MI, true);
            row.kph = 36; row.chg = true;
            const SegRowText whole = segRowText(seg, row, 0, RALLY_GREEK, DIST_KM3, false);
            return g.kph == "32.75" && m.kph == "20.35" && whole.kph == "36";
        });

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

        suite->addTest("distances: ; separates, whole metres", []() {
            auto p = parseStageDistances("1000;1500; 2300");
            return p.accepted.size() == 3 && p.accepted[0] == 1000 && p.accepted[2] == 2300
                && p.rest.empty() && p.problem.empty();
        });

        // Owner, 2026-11-15: a comma is never accepted (550,1000 was stored as one end).
        suite->addTest("distances: a comma is refused in every unit, nothing after it is taken", []() {
            for (int unit : {DIST_M, DIST_KM, DIST_KM3, DIST_MI, DIST_MI2}) {
                auto p = parseStageDistances("550,1000;2000", unit);
                if (!p.accepted.empty() || p.rest != "550,1000;2000"
                    || p.problem.find("commas not accepted") == std::string::npos) return false;
            }
            return true;
        });

        suite->addTest("distances: a decimal point is refused in metres mode, kept in km and miles", []() {
            auto m = parseStageDistances("1000;10.5", DIST_M);
            auto k = parseStageDistances("10.5", DIST_KM3);
            return m.accepted.size() == 1 && m.rest == "10.5"
                && m.problem.find("whole metres only") != std::string::npos
                && k.accepted.size() == 1 && k.accepted[0] == 10500 && k.problem.empty();
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
            auto p = parseStageDistances("17.23;17.5", true);
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

        suite->addTest("the refused-distance notice is in km when distance is in km", []() {
            auto v = sample();
            std::string m = insertStageDistances(v, 0, {1500});
            std::string k = insertStageDistances(v, 0, {1500}, true);
            return m.find(" m.") != std::string::npos && k.find("1.50") != std::string::npos
                && k.find(" km.") != std::string::npos && m.find("km") == std::string::npos;
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
                && formatDistance(125, true) == "0.12" && formatDistance(5, true) == "0.00"   // truncated, never rounded up
                && formatDistance(-5, true) == "0.00" && formatDistance(-12, true) == "-0.01"
                && formatDistance(127480, false) == "127,480" && formatDistance(127480, true) == "127.48";
        });

        // RB-WEB: the phone shows the box's own Segments text, made by one function for both.
        suite->addTest("segment row text: greek as the box shows it", []() {
            std::vector<Segment> segs(2);
            segs[0].distance_m = 380; segs[0].target_speed_kph = 32.75;
            segs[1].distance_m = 17230 - 380; segs[1].target_speed_kph = 32.75; segs[1].changesSpeed = false;   // ditto
            std::vector<StageRow> rows = buildStageRows(segs, 1, 0);
            SegRowText a = segRowText(segs[0], rows[0], 0, RALLY_GREEK, false);
            SegRowText b = segRowText(segs[1], rows[1], 1, RALLY_GREEK, false);
            return a.num == "1" && a.from == "0" && a.to == "380" && a.dist == "380"
                && a.kph == "32.75" && a.dur == formatHMmSs(rows[0].time_s) && a.tot == formatHMmSs(rows[0].total_s)
                && b.to == "17,230" && b.dist == "16,850" && b.kph == "\"" && b.ref.empty()
                && a.dur.size() == 7;   // h:mm:ss, as the box
        });

        suite->addTest("segment row text: italian as the box shows it", []() {
            std::vector<Segment> segs(3);
            segs[0].type = SEG_NAVI; segs[0].distance_m = 380; segs[0].target_speed_kph = 32.75;
            segs[1].type = SEG_PC; segs[1].distance_m = 180; segs[1].ref = 1; segs[1].pc_duration_s = 31;
            segs[1].target_speed_kph = 20.9032;
            segs[2].type = SEG_PM; segs[2].distance_m = 500; segs[2].ref = 2; segs[2].target_speed_kph = 30;
            std::vector<StageRow> rows = buildStageRows(segs, 1, 0);
            SegRowText n = segRowText(segs[0], rows[0], 0, RALLY_ITALIAN, true);
            SegRowText p = segRowText(segs[1], rows[1], 1, RALLY_ITALIAN, true);
            SegRowText m = segRowText(segs[2], rows[2], 2, RALLY_ITALIAN, true);
            return n.num == "1" && n.ref.empty() && n.kph == "32.75" && n.dur == formatHMmSs(rows[0].time_s)
                && p.num == "2" && p.ref == "1" && p.kph == "20.90" && p.dur == "0:31"
                && p.from == "0.38" && p.to == "0.56" && p.dist == "0.18"
                && m.ref == "2" && m.kph == "30.00" && m.tot == formatHMmSs(rows[2].total_s);
        });

        suite->addTest("segment row speeds follow kph / mph (stored in km/h)", []() {
            std::vector<Segment> segs(2);
            segs[0].distance_m = 380; segs[0].target_speed_kph = 64.37376;   // 40 mph typed
            segs[1].type = SEG_PM; segs[1].distance_m = 500; segs[1].ref = 2; segs[1].target_speed_kph = 50;
            std::vector<StageRow> rows = buildStageRows(segs, 1, 0);
            SegRowText g = segRowText(segs[0], rows[0], 0, RALLY_GREEK, false, true);
            SegRowText g2 = segRowText(segs[0], rows[0], 0, RALLY_GREEK, false, false);
            SegRowText it = segRowText(segs[1], rows[1], 1, RALLY_ITALIAN, true, true);
            return g.kph == "40" && g2.kph == "64.37"   /* two decimals at most (owner, 2026-11-15) */ && it.kph == "31.07";
        });

        // Miles (RB-SET-14): three decimals cut down, two from 100 miles; the box still counts in mm.
        suite->addTest("miles: figures as shown, cut down not rounded up", []() {
            return formatDistance(1609.344, DIST_MI) == "1.000" && formatDistance(0, DIST_MI) == "0.000"
                && formatDistance(1609.344 * 1.2345, DIST_MI) == "1.234"          // cut down, never rounded up
                && formatDistance(1608.0, DIST_MI) == "0.999"
                && formatDistance(160934.4, DIST_MI) == "100.00"                  // two decimals from 100 miles
                && formatDistance(1609.344 * 123.456, DIST_MI) == "123.45"
                && formatDistance(1609.344 * 99.999, DIST_MI) == "99.999"
                && formatDistance(-1609.344 * 2.5, DIST_MI) == "-2.500" && formatDistance(-0.4, DIST_MI) == "0.000"
                && formatDistance(125, true) == "0.12" && formatDistance(1234, false) == "1,234"   // km and metres unchanged
                && std::string(distUnitName(DIST_MI)) == "mi" && std::string(distUnitName(DIST_KM)) == "km"
                && std::string(distUnitName(DIST_M)) == "m"
                && distUnitToMetres(DIST_MI, 2.0) == 3218.688 && distUnitToMetres(DIST_KM, 2.0) == 2000.0;
        });

        suite->addTest("distance format and unit: all five combinations", []() {
            // km: 0.00 to the 10 m, 0.000 to the metre, 0,000 whole metres. mi: 0.00 and 0.000. No 0,000 in miles.
            return formatDistance(12345, DIST_KM) == "12.34" && formatDistance(12345, DIST_KM3) == "12.345"
                && formatDistance(12345, DIST_M) == "12,345"
                && formatDistance(1609.344 * 12.345, DIST_MI) == "12.345" && formatDistance(1609.344 * 12.345, DIST_MI2) == "12.34"
                && formatDistance(123456, DIST_KM3) == "123.45"                   // two decimals from 100 km
                && formatDistance(-2500, DIST_KM3) == "-2.500" && formatDistance(0.4, DIST_KM3) == "0.000"
                && formatDistance(1609.344 * 0.019, DIST_MI2) == "0.01"
                && distUnitFrom(2, false) == DIST_KM && distUnitFrom(3, false) == DIST_KM3 && distUnitFrom(0, false) == DIST_M
                && distUnitFrom(2, true) == DIST_MI2 && distUnitFrom(3, true) == DIST_MI
                && distUnitFrom(0, true) == DIST_MI                                // 0,000 does not exist in miles
                && distDecimals(DIST_M) == 0 && distDecimals(DIST_KM) == 2 && distDecimals(DIST_MI2) == 2
                && distDecimals(DIST_KM3) == 3 && distDecimals(DIST_MI) == 3
                && distIsMiles(DIST_MI2) && distIsMiles(DIST_MI) && !distIsMiles(DIST_KM3)
                && std::string(distUnitName(DIST_KM3)) == "km" && std::string(distUnitName(DIST_MI2)) == "mi"
                && parseDistanceExact("12.345", DIST_KM3) == 12345.0
                && formatDistance(parseDistanceExact("1.23", DIST_MI2), DIST_MI2) == "1.23"
                && startDistanceText(12345, DIST_KM3) == "12.345 km";
        });

        suite->addTest("changing the distance format or unit never changes programmed figures", []() {
            // A stage typed in km to three decimals: 8.345, 12.001.
            DistanceParse pr = parseStageDistances("8.345;12.001", DIST_KM3);
            std::vector<Segment> segs;
            insertStageDistances(segs, 0.0, pr.accepted, DIST_KM3);
            const double a0 = segs[0].distance_m, b0 = segs[1].distance_m;
            // Shown in every format in turn, each field tapped and left as the box wrote it: nothing is written back.
            for (int unit : {DIST_KM, DIST_M, DIST_MI, DIST_MI2, DIST_KM3}) {
                std::vector<StageRow> rows = buildStageRows(segs, 1, 0);
                for (size_t i = 0; i < rows.size(); i++)
                    if (!distanceFieldUntouched(formatDistance(rows[i].to_m, unit), rows[i].to_m, unit)) return false;
            }
            std::vector<StageRow> rows = buildStageRows(segs, 1, 0);
            return segs[0].distance_m == a0 && segs[1].distance_m == b0
                && formatDistance(rows[0].to_m, DIST_KM) == "8.34" && formatDistance(rows[0].to_m, DIST_KM3) == "8.345"
                && formatDistance(rows[1].to_m, DIST_KM3) == "12.001"
                // typed text is not "untouched": a real edit still goes through
                && !distanceFieldUntouched("8.35", rows[0].to_m, DIST_KM) && distanceFieldUntouched("8.34", rows[0].to_m, DIST_KM)
                && distanceFieldUntouched("8,345", 8345, DIST_M) && distanceFieldUntouched("5.185 mi", 8345, DIST_MI);
        });

        // The distance at start is held in whole metres and drawn to the nearest: 1.236 mi is stored as 1989 m, which a
        // cut-down figure would read as 1.235. Left as drawn, the field is untouched.
        suite->addTest("distance at start drawn to the nearest reads back untouched", []() {
            return startDistanceText(1989, DIST_MI) == "1.236 mi"
                && distanceFieldUntouched("1.236 mi", 1989, DIST_MI, true)
                && !distanceFieldUntouched("1.237 mi", 1989, DIST_MI, true);
        });

        suite->addTest("miles: every thousandth typed reads back as typed, to the millimetre and to the metre", []() {
            for (long t = 0; t <= 99999; t++) {
                char typed[16];
                snprintf(typed, sizeof(typed), "%ld.%03ld", t / 1000, t % 1000);
                const double exact_m = parseDistanceExact(typed, DIST_MI);          // rows: kept to the millimetre
                const long whole_m = parseDistance(typed, DIST_MI);                 // Distance at start: whole metres
                if (formatDistance(exact_m, DIST_MI) != typed) return false;
                if (formatDistance(static_cast<double>(whole_m), DIST_MI, true) != typed) return false;   // start distance: shown to the nearest
            }
            return parseDistanceExact("1,234 mi", DIST_MI) == parseDistanceExact("1.234", DIST_MI)
                && parseDistanceExact("x", DIST_MI) < 0 && parseDistanceExact("12.5", DIST_KM) == 12500.0
                && parseDistance("1,250", DIST_M) == 1250;
        });

        suite->addTest("miles: a stage typed in miles keeps every to figure and every interval", []() {
            DistanceParse pr = parseStageDistances("0.250; 1.234; 2.500;10.001", DIST_MI);
            if (pr.accepted.size() != 4) return false;
            std::vector<Segment> segs;
            insertStageDistances(segs, 0.0, pr.accepted, DIST_MI);
            std::vector<StageRow> rows = buildStageRows(segs, 1, 0);
            if (rows.size() != 4) return false;
            const SegRowText a = segRowText(segs[0], rows[0], 0, RALLY_GREEK, DIST_MI);
            const SegRowText b = segRowText(segs[1], rows[1], 1, RALLY_GREEK, DIST_MI);
            const SegRowText c = segRowText(segs[2], rows[2], 2, RALLY_GREEK, DIST_MI);
            const SegRowText d = segRowText(segs[3], rows[3], 3, RALLY_GREEK, DIST_MI);
            return a.to == "0.250" && b.to == "1.234" && c.to == "2.500" && d.to == "10.001"
                && a.dist == "0.250" && b.dist == "0.984" && c.dist == "1.266" && d.dist == "7.501"
                && startDistanceText(27000, DIST_MI) == "16.777 mi" && startDistanceText(27000, DIST_KM) == "27.00 km";
        });

        suite->addTest("segments side panel text as the box shows it", []() {
            std::vector<Segment> segs(2);
            segs[0].distance_m = 50000; segs[1].distance_m = 46240;
            return sectorAverageText(segs, 3 * 3600) == "Average 32.08"
                && sectorAverageText(segs, 0).empty() && sectorAverageText({}, 3600).empty()
                && startDistanceText(6000, false) == "6,000 m" && startDistanceText(6000, true) == "6.00 km"
                && startDistanceText(0, false).empty();
        });

        // The box's own texts, sent to the phone as they are (the phone never reformats).
        suite->addTest("driver averages read two decimals", []() {
            return averageSpeedText(32.0833) == "32.08" && averageSpeedText(0) == "0.00"
                && averageSpeedText(9.999) == "10.00";
        });

        suite->addTest("arrival speed text: kph whole, mph rounded", []() {
            return arrivalSpeedText(32.9, false) == "32" && arrivalSpeedText(50, true) == "31"
                && arrivalSpeedText(-1, false) == "--";
        });

        suite->addTest("rally date reads as the Time page's", []() {
            const std::string d = rallyDateText(1700000000000LL);   // a Tuesday or Wednesday, Nov 2023, by zone
            return d.size() == 15 && d[3] == ' ' && d.find(',') == std::string::npos
                && d.substr(d.size() - 4) == "2023" && d.find("Nov") != std::string::npos;
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
