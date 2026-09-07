#ifndef TEST_BEEP_ASSIST_H
#define TEST_BEEP_ASSIST_H

#include "test_framework.h"
#include "../calculations.h"
#include <string>
#include <vector>

// Tests for Beep Assist: operator-entered waypoint distances that sound a
// beep as they are reached, optionally brought forward by a fixed distance
// (navigation mode) or a fixed number of seconds off the scheduled
// (roadbook) arrival time, computed from segment speed/distance (timing
// mode, only meaningful during a stage).
class TestBeepAssist {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Beep Assist");

        // ---- parsing ----

        suite->addTest("parses a comma-separated kilometre list into metres", []() {
            std::vector<double> w = parseBeepWaypointsKm("3.67, 4.98, 6.06");
            return w.size() == 3
                && std::abs(w[0] - 3670.0) < 0.001
                && std::abs(w[1] - 4980.0) < 0.001
                && std::abs(w[2] - 6060.0) < 0.001;
        });

        suite->addTest("accepts semicolons and spaces as separators too", []() {
            // The numeric keypad offers ";", so both must work.
            std::vector<double> w = parseBeepWaypointsKm("1.5; 2.5 3.5");
            return w.size() == 3;
        });

        suite->addTest("sorts waypoints into travel order", []() {
            // Entered out of order, they must still fire in the order the car
            // reaches them.
            std::vector<double> w = parseBeepWaypointsKm("6.06, 3.67, 4.98");
            return w.size() == 3 && w[0] < w[1] && w[1] < w[2];
        });

        suite->addTest("drops duplicates", []() {
            // A duplicate would beep twice at the same point.
            std::vector<double> w = parseBeepWaypointsKm("3.67, 3.67, 4.98");
            return w.size() == 2;
        });

        suite->addTest("skips a malformed token instead of losing the list", []() {
            // One typo must not cost the operator every other waypoint.
            std::vector<double> w = parseBeepWaypointsKm("3.67, banana, 4.98");
            return w.size() == 2
                && std::abs(w[0] - 3670.0) < 0.001
                && std::abs(w[1] - 4980.0) < 0.001;
        });

        suite->addTest("drops negative waypoints", []() {
            return parseBeepWaypointsKm("-2.0, 3.67").size() == 1;
        });

        suite->addTest("an empty list parses to no waypoints", []() {
            return parseBeepWaypointsKm("").empty()
                && parseBeepWaypointsKm("   ,  ; ").empty();
        });

        suite->addTest("round-trips back to the display form", []() {
            std::vector<double> w = parseBeepWaypointsKm("3.67, 4.98, 17.30");
            return formatBeepWaypointsKm(w) == "3.67, 4.98, 17.30";
        });

        // ---- the cursor ----

        suite->addTest("cursor starts at the first waypoint before the start", []() {
            std::vector<double> w = { 1000.0, 2000.0, 3000.0 };
            return beepCursorFor(w, 0.0) == 0;
        });

        suite->addTest("cursor skips waypoints already passed", []() {
            // A restart mid-stage must not replay every waypoint behind the car.
            std::vector<double> w = { 1000.0, 2000.0, 3000.0 };
            return beepCursorFor(w, 2500.0) == 2;
        });

        suite->addTest("cursor runs off the end once all are passed", []() {
            std::vector<double> w = { 1000.0, 2000.0 };
            return beepCursorFor(w, 9000.0) == 2;
        });

        // ---- idealSecondsToReachDistance ----
        // The scheduled (roadbook) time to cover a distance, from each loaded
        // segment's own target_speed_kph / distance_m -- distance actually
        // travelled never enters this calculation.

        suite->addTest("computes time to a target within a single segment", []() {
            std::vector<Segment> segs;
            Segment s{}; s.target_speed_kph = 36.0; s.distance_m = 1000.0;  // 10 m/s
            segs.push_back(s);
            return std::abs(idealSecondsToReachDistance(segs, 500.0) - 50.0) < 0.01;
        });

        suite->addTest("sums whole segments before the one the target falls in", []() {
            std::vector<Segment> segs;
            Segment a{}; a.target_speed_kph = 36.0; a.distance_m = 1000.0;  // 10 m/s, 100 s
            Segment b{}; b.target_speed_kph = 72.0; b.distance_m = 1000.0;  // 20 m/s
            segs.push_back(a); segs.push_back(b);
            // All of a (100 s) + 200 m into b at 20 m/s (10 s) = 110 s.
            return std::abs(idealSecondsToReachDistance(segs, 1200.0) - 110.0) < 0.01;
        });

        suite->addTest("a target beyond the loaded segments is undefined", []() {
            std::vector<Segment> segs;
            Segment s{}; s.target_speed_kph = 36.0; s.distance_m = 1000.0;
            segs.push_back(s);
            return idealSecondsToReachDistance(segs, 5000.0) < 0.0;
        });

        suite->addTest("an empty segment list is undefined", []() {
            std::vector<Segment> segs;
            return idealSecondsToReachDistance(segs, 100.0) < 0.0;
        });

        suite->addTest("a target past a zero-speed segment is undefined, not silently early", []() {
            // A segment with a real distance but no target speed is an
            // incomplete roadbook, not a zero-length one. Skipping it would
            // drop its DISTANCE as well as its time, shortening the roadbook
            // and firing every later timing beep early by the missing
            // segment's duration. Undefined is the honest answer.
            std::vector<Segment> segs;
            Segment bad{}; bad.target_speed_kph = 0.0; bad.distance_m = 500.0;
            Segment good{}; good.target_speed_kph = 36.0; good.distance_m = 1000.0;  // 10 m/s
            segs.push_back(bad); segs.push_back(good);
            return idealSecondsToReachDistance(segs, 300.0) < 0.0;
        });

        suite->addTest("a target before a zero-speed segment still resolves", []() {
            // The incomplete segment only poisons distances at or beyond it.
            std::vector<Segment> segs;
            Segment good{}; good.target_speed_kph = 36.0; good.distance_m = 1000.0;  // 10 m/s
            Segment bad{}; bad.target_speed_kph = 0.0; bad.distance_m = 500.0;
            segs.push_back(good); segs.push_back(bad);
            return std::abs(idealSecondsToReachDistance(segs, 500.0) - 50.0) < 0.01;
        });

        suite->addTest("a zero-LENGTH segment is skipped rather than poisoning the roadbook", []() {
            // distance_m == 0 genuinely contributes nothing, whatever its
            // speed says; it must not be confused with the case above.
            std::vector<Segment> segs;
            Segment empty{}; empty.target_speed_kph = 0.0; empty.distance_m = 0.0;
            Segment good{}; good.target_speed_kph = 36.0; good.distance_m = 1000.0;  // 10 m/s
            segs.push_back(empty); segs.push_back(good);
            return std::abs(idealSecondsToReachDistance(segs, 300.0) - 30.0) < 0.01;
        });

        // ---- the timing cursor ----
        // Timing mode is ordered by SCHEDULED time, not by distance, so its
        // cursor cannot be derived with beepCursorFor. Running ahead of
        // schedule, a distance-derived cursor steps past waypoints whose
        // roadbook time has not arrived and drops their beep entirely.

        suite->addTest("timing cursor stops at the first waypoint whose time has not come", []() {
            std::vector<double> w = { 1000.0, 2000.0, 3000.0 };
            std::vector<Segment> segs;
            Segment s{}; s.target_speed_kph = 36.0; s.distance_m = 5000.0;  // 10 m/s
            segs.push_back(s);
            // Scheduled times: 100 s, 200 s, 300 s. At 150 s elapsed only the
            // first waypoint is behind us.
            return beepTimingCursorFor(w, 150.0, segs, 0.0) == 1;
        });

        suite->addTest("timing cursor ignores distance already travelled", []() {
            // The regression: the car is 2500 m along but only 50 s into the
            // stage. A distance-derived cursor would return 2 and silently
            // eat the 1000 m and 2000 m timing beeps.
            std::vector<double> w = { 1000.0, 2000.0, 3000.0 };
            std::vector<Segment> segs;
            Segment s{}; s.target_speed_kph = 36.0; s.distance_m = 5000.0;  // 10 m/s
            segs.push_back(s);
            return beepTimingCursorFor(w, 50.0, segs, 0.0) == 0;
        });

        suite->addTest("timing cursor honours the lead-in", []() {
            std::vector<double> w = { 1000.0, 2000.0 };
            std::vector<Segment> segs;
            Segment s{}; s.target_speed_kph = 36.0; s.distance_m = 5000.0;  // 10 m/s
            segs.push_back(s);
            // 1000 m is due at 100 s; with a 30 s lead-in it is already due
            // at 80 s, so the cursor has moved past it.
            return beepTimingCursorFor(w, 80.0, segs, 30.0) == 1;
        });

        suite->addTest("timing cursor stops at a waypoint the segments do not reach", []() {
            // Undefined scheduled time means not yet due, so the cursor holds
            // there rather than stepping over it.
            std::vector<double> w = { 1000.0, 9000.0 };
            std::vector<Segment> segs;
            Segment s{}; s.target_speed_kph = 36.0; s.distance_m = 5000.0;  // 10 m/s
            segs.push_back(s);
            return beepTimingCursorFor(w, 100000.0, segs, 0.0) == 1;
        });

        // ---- navigationBeepDue ----
        // Pure distance -- current speed plays no part at all.

        suite->addTest("navigation mode is due once travelled reaches the lead-in point", []() {
            return navigationBeepDue(1000.0, 800.0, 200.0)
                && !navigationBeepDue(1000.0, 799.0, 200.0);
        });

        suite->addTest("with zero lead-in, navigation mode is due exactly at the waypoint", []() {
            return navigationBeepDue(1000.0, 1000.0, 0.0)
                && !navigationBeepDue(1000.0, 999.0, 0.0);
        });

        // ---- timingBeepDue ----

        suite->addTest("timing mode is due once elapsed time reaches the lead-in point", []() {
            std::vector<Segment> segs;
            Segment s{}; s.target_speed_kph = 36.0; s.distance_m = 1000.0;  // ideal 100 s
            segs.push_back(s);
            return timingBeepDue(1000.0, 95.0, segs, 5.0)
                && !timingBeepDue(1000.0, 94.0, segs, 5.0);
        });

        suite->addTest("timing mode never fires for a waypoint beyond the loaded segments", []() {
            std::vector<Segment> segs;
            Segment s{}; s.target_speed_kph = 36.0; s.distance_m = 1000.0;
            segs.push_back(s);
            return !timingBeepDue(5000.0, 99999.0, segs, 0.0);
        });

        // ---- dueBeepWaypoint ----
        // Orchestrates both modes over the waypoint list: either firing is
        // enough, and the caller advances from_index past whatever fires, so
        // there is no need to compare the two triggers against each other.

        suite->addTest("fires on reaching a waypoint with navigation mode and no lead-in", []() {
            std::vector<double> w = { 1000.0, 2000.0 };
            std::vector<Segment> segs;
            return dueBeepWaypoint(w, 0, 1000.0, true, 0.0, false, 0.0, false, 0.0, segs) == 0
                && dueBeepWaypoint(w, 0, 999.0, true, 0.0, false, 0.0, false, 0.0, segs) == -1;
        });

        suite->addTest("navigation mode fires a fixed distance early", []() {
            std::vector<double> w = { 1000.0 };
            std::vector<Segment> segs;
            return dueBeepWaypoint(w, 0, 800.0, true, 200.0, false, 0.0, false, 0.0, segs) == 0
                && dueBeepWaypoint(w, 0, 799.0, true, 200.0, false, 0.0, false, 0.0, segs) == -1;
        });

        suite->addTest("timing mode fires the scheduled time early, during a stage", []() {
            std::vector<double> w = { 1000.0 };
            std::vector<Segment> segs;
            Segment s{}; s.target_speed_kph = 36.0; s.distance_m = 1000.0;  // ideal 100 s
            segs.push_back(s);
            return dueBeepWaypoint(w, 0, 0.0, false, 0.0, true, 5.0, true, 95.0, segs) == 0
                && dueBeepWaypoint(w, 0, 0.0, false, 0.0, true, 5.0, true, 94.0, segs) == -1;
        });

        suite->addTest("timing mode does nothing outside a stage even if the scheduled time has passed", []() {
            std::vector<double> w = { 1000.0 };
            std::vector<Segment> segs;
            Segment s{}; s.target_speed_kph = 36.0; s.distance_m = 1000.0;
            segs.push_back(s);
            return dueBeepWaypoint(w, 0, 0.0, false, 0.0, true, 0.0, false, 99999.0, segs) == -1;
        });

        suite->addTest("with both modes on, whichever condition is met first fires", []() {
            // Navigation due at 800 m travelled; timing not due yet (10 s
            // elapsed against a 100 s ideal). Navigation fires the waypoint.
            std::vector<double> w = { 1000.0 };
            std::vector<Segment> segs;
            Segment s{}; s.target_speed_kph = 36.0; s.distance_m = 1000.0;
            segs.push_back(s);
            return dueBeepWaypoint(w, 0, 800.0, true, 200.0, true, 5.0, true, 10.0, segs) == 0;
        });

        suite->addTest("the cursor stops a waypoint firing twice", []() {
            std::vector<double> w = { 1000.0, 2000.0 };
            std::vector<Segment> segs;
            return dueBeepWaypoint(w, 1, 1500.0, true, 0.0, false, 0.0, false, 0.0, segs) == -1;
        });

        suite->addTest("a cursor past the end never fires", []() {
            std::vector<double> w = { 1000.0 };
            std::vector<Segment> segs;
            return dueBeepWaypoint(w, 1, 9999.0, true, 0.0, false, 0.0, false, 0.0, segs) == -1;
        });

        suite->addTest("catches up when several waypoints are passed at once", []() {
            // A long polling gap must report the earliest missed waypoint, not
            // skip silently to the newest.
            std::vector<double> w = { 1000.0, 2000.0, 3000.0 };
            std::vector<Segment> segs;
            return dueBeepWaypoint(w, 0, 2500.0, true, 0.0, false, 0.0, false, 0.0, segs) == 0;
        });

        suite->addTest("an empty waypoint list never fires", []() {
            std::vector<double> w;
            std::vector<Segment> segs;
            return dueBeepWaypoint(w, 0, 9999.0, true, 100.0, true, 5.0, true, 99999.0, segs) == -1;
        });

        return suite;
    }
};

#endif // TEST_BEEP_ASSIST_H
