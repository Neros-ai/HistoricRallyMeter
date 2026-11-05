#ifndef TEST_DISTANCE_ADJUST_H
#define TEST_DISTANCE_ADJUST_H

#include "test_framework.h"
#include "../calculations.h"

// Tests for the co-pilot's manual distance correction. The correction is held
// in centimetres alongside the counter baseline, so a wheel-slip or roadbook
// discrepancy can be dialled out without resetting the counter and losing the
// elapsed time with it.
class TestDistanceAdjust {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Distance Adjustment");

        suite->addTest("a positive correction adds to the reading", []() {
            // 1,000 m raw + 10 m correction = 1,010 m
            return adjustedDistanceMetersMm(1000000, 10000) == 1010;
        });

        suite->addTest("a negative correction subtracts from the reading", []() {
            return adjustedDistanceMetersMm(1000000, -10000) == 990;
        });

        suite->addTest("no correction leaves the reading alone", []() {
            return adjustedDistanceMetersMm(1000000, 0) == 1000;
        });

        suite->addTest("truncates to whole metres like the raw reading", []() {
            // 1,000.99 m must read 1,000 m, not 1,001 -- the same truncation
            // the uncorrected path already applies.
            return adjustedDistanceMetersMm(1000990, 0) == 1000;
        });

        suite->addTest("a correction cannot drive the reading negative", []() {
            return adjustedDistanceMetersMm(5000, -100000) == -95;   // reversed past the start: negative metres
        });

        suite->addTest("clamps a correction that would go below zero", []() {
            // 5 m travelled, operator presses -10: the most that can be taken
            // off is the 5 m actually there.
            return clampDistanceAdjust(5000, -10000) == -5000;
        });

        suite->addTest("leaves a correction that stays above zero alone", []() {
            return clampDistanceAdjust(1000000, -10000) == -10000
                && clampDistanceAdjust(1000000, 10000) == 10000;
        });

        suite->addTest("clamps to exactly zero at the boundary", []() {
            return clampDistanceAdjust(10000, -10000) == -10000;
        });

        suite->addTest("repeated downward presses cannot bank hidden debt", []() {
            // Three -10 presses against 5 m travelled must leave the same
            // correction as one, or the next 25 m of real travel vanishes.
            long raw = 5000;
            long adjust = 0;
            for (int i = 0; i < 3; i++)
                adjust += clampDistanceAdjust(raw + adjust, -10000);
            return adjust == -5000 && adjustedDistanceMetersMm(raw, adjust) == 0;
        });

        suite->addTest("the correction reaches the stage position, not just the odometer", []() {
            // calibration is mm per 1000 counts, so 3600 is 3.6 mm a count: 100,000 counts = 360 m.
            RallyState s;
            s.calibration = 3600; s.counters = false;
            s.total_start_cntr1 = 0; s.total_start_cntr2 = 0;
            // No correction changes nothing.
            ASSERT_EQ(stagePositionMm(s, 100000, 100000), 360000);
            // Five presses of -10 take 50 m off the stage position the ahead/behind figure, the segment
            // boundary and the countdown are measured against, not just the odometer, and +50 m adds exactly as much.
            s.total_distance_adjust_mm = -50000;
            ASSERT_EQ(stagePositionMm(s, 100000, 100000), 310000);
            s.total_distance_adjust_mm = 50000;
            ASSERT_EQ(stagePositionMm(s, 100000, 100000), 410000);
            // Consistent with what the readout shows for the same correction.
            ASSERT_EQ(adjustedDistanceMetersMm(countsToMm(100000, 3600), -50000), 310);
            return true;
        });

        suite->addTest("an over-correction reads negative (distances go below zero on reverse)", []() {
            RallyState s;
            s.calibration = 3600; s.counters = false;
            s.total_distance_adjust_mm = -5000000;      // 5 km off a 0.36 m drive
            return stagePositionMm(s, 100, 100) == 360 - 5000000 && adjustedDistanceMetersMm(360, -5000000) == -4999;
        });

        return suite;
    }
};

#endif // TEST_DISTANCE_ADJUST_H
