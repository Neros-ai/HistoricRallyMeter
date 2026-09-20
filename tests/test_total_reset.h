#ifndef TEST_TOTAL_RESET_H
#define TEST_TOTAL_RESET_H

#include "test_framework.h"
#include "../calculations.h"
#include "../rally_state.h"

// Reset Total by situation, and the Trip baselines at a segment change --
// contingencies 2, 4 and 6 of the usership prompt in docs/HANDOFF.md.
class TestTotalReset {
    // A stage part-way through segment 2, with a -10 correction on both
    // counters and nothing armed.
    static RallyState runningStage() {
        RallyState s;
        s.total_start_cntr1 = s.total_start_cntr2 = 1000;
        s.total_start_time_ms = 100000;
        s.trip_start_cntr1 = s.trip_start_cntr2 = 3000;
        s.trip_start_time_ms = 200000;
        s.segment_start_cntr1 = s.segment_start_cntr2 = 3000;
        s.segment_start_time_ms = 200000;
        s.segment_current_number = 1;
        s.stage_complete = false;
        s.total_distance_adjust_cm = -1000;
        s.trip_distance_adjust_cm = -1000;
        s.segment_start_adjust_cm = 0;
        return s;
    }

public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Total Reset Tests");

        // RB-NAV-14. The alarm stores the odometer reading it fires at,
        // measured from the Total counter's zero -- and a reset moves that
        // zero. Left alone, a 20 km alarm set at 5 km fired 25 km after the
        // reset instead of the 20 km that was actually left to run.
        suite->addTest("a Total reset leaves the alarm's remaining distance alone", []() {
            RallyState s;
            s.counters = false;                 // counter 1 alone
            s.total_start_cntr1 = s.total_start_cntr2 = 1000;
            s.alarm_distance_km = 20;
            s.alarm_target_counts = 25000;      // set when 5000 counts were up
            // Reset with the car at 6000: 5000 counts covered since the zero.
            rebaseTotalDistance(s, 6000, 6000);
            ASSERT_EQ(s.alarm_target_counts, 20000);   // still 20000 to run
            return true;
        });

        suite->addTest("a Total reset past the alarm leaves it at zero, not negative", []() {
            RallyState s;
            s.counters = false;
            s.total_start_cntr1 = s.total_start_cntr2 = 1000;
            s.alarm_distance_km = 20;
            s.alarm_target_counts = 5000;
            rebaseTotalDistance(s, 20000, 20000);      // 19000 covered
            ASSERT_EQ(s.alarm_target_counts, 0);
            return true;
        });

        suite->addTest("a Total reset with no alarm set touches nothing", []() {
            RallyState s;
            s.counters = false;
            s.total_start_cntr1 = s.total_start_cntr2 = 1000;
            s.alarm_distance_km = 0;
            s.alarm_target_counts = 0;
            rebaseTotalDistance(s, 6000, 6000);
            ASSERT_EQ(s.alarm_target_counts, 0);
            return true;
        });

        suite->addTest("an on-the-minute autostart outranks a running stage", []() {
            // Arming it loaded the new stage: the crew are at its line.
            RallyState s;
            s.stage_complete = false;
            s.segment_current_number = 0;
            s.auto_start_rally_time_s = 1000;
            s.auto_start_early_departure = true;
            ASSERT_TRUE(classifyTotalReset(s) == TotalResetCase::AwaitingEarlyDeparture);
            s.auto_start_rally_time_s = 0;
            ASSERT_TRUE(classifyTotalReset(s) == TotalResetCase::StageRunning);
            return true;
        });

        suite->addTest("a set-time autostart outranks a stage still on the gauge", []() {
            // Armed while the previous stage runs on: the display counts down
            // to the next start, so Total is the waiting-for-autostart reset,
            // with no question (owner's ruling).
            RallyState s;
            s.stage_complete = false;
            s.segment_current_number = 2;
            s.auto_start_rally_time_s = 1000;
            s.auto_start_early_departure = false;
            ASSERT_TRUE(classifyTotalReset(s) == TotalResetCase::AwaitingTimedStart);
            // With no stage running it is the line of the timed start.
            s.stage_complete = true;
            s.segment_current_number = -1;
            ASSERT_TRUE(classifyTotalReset(s) == TotalResetCase::AwaitingTimedStart);
            return true;
        });

        suite->addTest("abort covers a stage on the gauge and any armed autostart", []() {
            RallyState s;
            ASSERT_FALSE(stageAbortable(s));            // idle: nothing to end
            s.segment_current_number = 1;
            s.stage_complete = false;
            ASSERT_TRUE(stageAbortable(s));             // running
            s.stage_complete = true;
            ASSERT_TRUE(stageAbortable(s));             // driven out, still on the gauge
            s.segment_current_number = -1;
            s.auto_start_rally_time_s = 1000;
            s.auto_start_early_departure = true;
            ASSERT_TRUE(stageAbortable(s));             // on the minute, counting down
            s.auto_start_early_departure = false;
            ASSERT_TRUE(stageAbortable(s));             // set time: cancelled too
            return true;
        });

        suite->addTest("no stage and nothing armed is the plain odometer case", []() {
            RallyState s;
            ASSERT_TRUE(classifyTotalReset(s) == TotalResetCase::Idle);
            return true;
        });

        suite->addTest("a stage driven out to its end is the plain idle reset", []() {
            RallyState s;
            s.segment_current_number = 2;
            s.stage_complete = true;
            ASSERT_TRUE(classifyTotalReset(s) == TotalResetCase::Idle);
            return true;
        });

        suite->addTest("a missed-start reset zeroes distance at the press and keeps the clock", []() {
            // Usership 6: the autostart fired before the car reached the
            // line. Total pressed there at counters 5000, confirmed later.
            RallyState s = runningStage();
            s.stage_segments.resize(3);
            applyDistanceResetKeepingClock(s, 5000, 5002, 250000);

            ASSERT_EQ(s.total_start_cntr1, 5000u);
            ASSERT_EQ(s.total_start_cntr2, 5002u);
            ASSERT_EQ(s.trip_start_cntr1, 5000u);
            ASSERT_EQ(s.segment_start_cntr1, 5000u);
            ASSERT_EQ(s.total_distance_adjust_cm, 0);
            ASSERT_EQ(s.trip_distance_adjust_cm, 0);
            ASSERT_EQ(s.segment_start_adjust_cm, 0);
            // The stage clock is untouched: the gauge shows the lateness.
            ASSERT_EQ(s.total_start_time_ms, 100000);
            ASSERT_EQ(s.segment_start_time_ms, 100000);
            ASSERT_EQ(s.trip_start_time_ms, 250000);
            // Back on the first segment, still running -- not aborted.
            ASSERT_EQ(s.segment_current_number, 0);
            ASSERT_FALSE(s.stage_complete);
            return true;
        });

        suite->addTest("reset awaiting the minute moves distance, not the clock or the stage", []() {
            // Contingency 4: the car crept at the line with "on the minute"
            // primed. Arming loaded segment 1; the reset must leave it loaded
            // so the driver panel keeps its speeds through the countdown.
            RallyState s;
            s.total_start_time_ms = 50000;      // previous stage's clock zero
            s.trip_start_time_ms = 50000;
            s.segment_current_number = 0;
            s.stage_complete = false;
            s.auto_start_rally_time_s = 1000;
            s.auto_start_early_departure = true;
            s.total_distance_adjust_cm = -500;
            s.trip_distance_adjust_cm = -500;

            applyTotalResetAwaitingEarlyDeparture(s, 7000, 7002, 90000);

            ASSERT_EQ(s.total_start_cntr1, 7000u);
            ASSERT_EQ(s.total_start_cntr2, 7002u);
            ASSERT_EQ(s.trip_start_cntr1, 7000u);
            ASSERT_EQ(s.segment_start_cntr1, 7000u);
            ASSERT_EQ(s.total_distance_adjust_cm, 0);
            ASSERT_EQ(s.trip_distance_adjust_cm, 0);
            ASSERT_EQ(s.segment_start_adjust_cm, 0);
            ASSERT_EQ(s.segment_start_time_ms, 90000);
            // The minute zeroes the clocks, not this press.
            ASSERT_EQ(s.total_start_time_ms, 50000);
            ASSERT_EQ(s.trip_start_time_ms, 50000);
            // Still loaded, still primed.
            ASSERT_EQ(s.segment_current_number, 0);
            ASSERT_FALSE(s.stage_complete);
            ASSERT_EQ(s.auto_start_rally_time_s, 1000u);
            ASSERT_TRUE(s.auto_start_early_departure);
            return true;
        });

        suite->addTest("reset awaiting a timed start zeroes Total and Trip and leaves it primed", []() {
            RallyState s = runningStage();
            s.auto_start_rally_time_s = 1000;
            s.auto_start_early_departure = false;

            applyTotalResetToIdle(s, 9000, 9000, 400000, true);

            ASSERT_EQ(s.total_start_cntr1, 9000u);
            ASSERT_EQ(s.trip_start_cntr1, 9000u);
            ASSERT_EQ(s.total_start_time_ms, 400000);
            ASSERT_EQ(s.trip_start_time_ms, 400000);
            ASSERT_EQ(s.trip_distance_adjust_cm, 0);
            ASSERT_EQ(s.segment_current_number, -1);
            ASSERT_TRUE(s.stage_complete);
            ASSERT_EQ(s.auto_start_rally_time_s, 1000u);
            return true;
        });

        suite->addTest("the Trip reset touches nothing that runs the stage", []() {
            RallyState s = runningStage();
            RallyState before = s;

            resetTrip(s, 5000, 5000, 300000);

            ASSERT_EQ(s.trip_start_cntr1, 5000u);
            ASSERT_EQ(s.trip_start_time_ms, 300000);
            ASSERT_EQ(s.trip_distance_adjust_cm, 0);
            ASSERT_EQ(s.total_start_cntr1, before.total_start_cntr1);
            ASSERT_EQ(s.total_start_time_ms, before.total_start_time_ms);
            ASSERT_EQ(s.total_distance_adjust_cm, before.total_distance_adjust_cm);
            ASSERT_EQ(s.segment_start_cntr1, before.segment_start_cntr1);
            ASSERT_EQ(s.segment_start_time_ms, before.segment_start_time_ms);
            ASSERT_EQ(s.segment_current_number, 1);
            ASSERT_FALSE(s.stage_complete);
            return true;
        });

        suite->addTest("an idle reset with Trip zeroes Total and Trip, distance and time, and ends any stage", []() {
            RallyState s = runningStage();

            applyTotalResetToIdle(s, 5000, 5000, 300000, true);

            ASSERT_EQ(s.total_start_cntr1, 5000u);
            ASSERT_EQ(s.segment_start_cntr1, 5000u);
            ASSERT_EQ(s.trip_start_cntr1, 5000u);
            ASSERT_EQ(s.total_start_time_ms, 300000);
            ASSERT_EQ(s.segment_start_time_ms, 300000);
            ASSERT_EQ(s.trip_start_time_ms, 300000);
            ASSERT_EQ(s.total_distance_adjust_cm, 0);
            ASSERT_EQ(s.trip_distance_adjust_cm, 0);
            ASSERT_EQ(s.segment_start_adjust_cm, 0);
            ASSERT_EQ(s.segment_current_number, -1);
            ASSERT_TRUE(s.stage_complete);
            return true;
        });

        suite->addTest("an idle reset leaves Trip alone, as it always has", []() {
            RallyState s;
            s.trip_start_cntr1 = s.trip_start_cntr2 = 1234;
            s.trip_start_time_ms = 777;
            s.trip_distance_adjust_cm = -300;

            applyTotalResetToIdle(s, 5000, 5000, 300000, false);

            ASSERT_EQ(s.total_start_cntr1, 5000u);
            ASSERT_EQ(s.total_start_time_ms, 300000);
            ASSERT_EQ(s.trip_start_cntr1, 1234u);
            ASSERT_EQ(s.trip_start_time_ms, 777);
            ASSERT_EQ(s.trip_distance_adjust_cm, -300);
            return true;
        });

        suite->addTest("a segment change spends Trip's correction with its baseline", []() {
            // Contingency 2: 50 m knocked off in segment 1 must not leave
            // Trip reading 50 m short all through segment 2.
            RallyState s = runningStage();
            s.trip_distance_adjust_cm = -5000;
            s.segment_start_time_ms = 250000;   // the new segment's start

            rebaseTripToSegment(s, 6000, 6004);

            ASSERT_EQ(s.trip_start_cntr1, 6000u);
            ASSERT_EQ(s.trip_start_cntr2, 6004u);
            ASSERT_EQ(s.trip_start_time_ms, 250000);
            ASSERT_EQ(s.trip_distance_adjust_cm, 0);
            // Total's correction still stands: it is stage distance.
            ASSERT_EQ(s.total_distance_adjust_cm, -1000);
            return true;
        });

        return suite;
    }
};

#endif // TEST_TOTAL_RESET_H
