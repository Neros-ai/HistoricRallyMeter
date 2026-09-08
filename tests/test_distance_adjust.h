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
            return adjustedDistanceMeters(100000, 1000) == 1010;
        });

        suite->addTest("a negative correction subtracts from the reading", []() {
            return adjustedDistanceMeters(100000, -1000) == 990;
        });

        suite->addTest("no correction leaves the reading alone", []() {
            return adjustedDistanceMeters(100000, 0) == 1000;
        });

        suite->addTest("truncates to whole metres like the raw reading", []() {
            // 1,000.99 m must read 1,000 m, not 1,001 -- the same truncation
            // the uncorrected path already applies.
            return adjustedDistanceMeters(100099, 0) == 1000;
        });

        suite->addTest("a correction cannot drive the reading negative", []() {
            return adjustedDistanceMeters(500, -10000) == 0;
        });

        suite->addTest("clamps a correction that would go below zero", []() {
            // 5 m travelled, operator presses -10: the most that can be taken
            // off is the 5 m actually there.
            return clampDistanceAdjust(500, -1000) == -500;
        });

        suite->addTest("leaves a correction that stays above zero alone", []() {
            return clampDistanceAdjust(100000, -1000) == -1000
                && clampDistanceAdjust(100000, 1000) == 1000;
        });

        suite->addTest("clamps to exactly zero at the boundary", []() {
            return clampDistanceAdjust(1000, -1000) == -1000;
        });

        suite->addTest("repeated downward presses cannot bank hidden debt", []() {
            // Three -10 presses against 5 m travelled must leave the same
            // correction as one, or the next 25 m of real travel vanishes.
            long raw = 500;
            long adjust = 0;
            for (int i = 0; i < 3; i++)
                adjust += clampDistanceAdjust(raw + adjust, -1000);
            return adjust == -500 && adjustedDistanceMeters(raw, adjust) == 0;
        });

        suite->addTest("the correction reaches the counts, not just the odometer", []() {
            // calibration is mm per 1000 counts, so 3600 is 0.36 cm/count.
            const long cal = 3600;
            const int64_t raw = 100000;                 // 36000 cm = 360 m
            // No correction changes nothing.
            ASSERT_EQ(correctedDistanceCounts(raw, 0, cal), raw);
            // The roundabout case from the crew's own worked example: five
            // presses of -10 take 50 m off, which at 0.36 cm/count is 13888
            // counts. That has to move the stage distance the ahead/behind
            // figure and the segment boundary are measured against, not just
            // the odometer -- and it must round the same way in both
            // directions rather than favouring one.
            ASSERT_EQ(correctedDistanceCounts(raw, -5000, cal), raw - 13888);
            ASSERT_EQ(correctedDistanceCounts(raw, 5000, cal), raw + 13888);
            // Consistent with what the odometer shows for the same correction.
            ASSERT_EQ(countsToCentimeters(correctedDistanceCounts(raw, -5000, cal), cal),
                      adjustedDistanceMeters(countsToCentimeters(raw, cal), -5000) * 100);
            return true;
        });

        suite->addTest("an over-correction reads as the start, never as negative", []() {
            // Matching adjustedDistanceMeters: a correction bigger than the
            // distance driven leaves the stage at zero rather than running the
            // roadbook backwards through the previous segment.
            ASSERT_EQ(correctedDistanceCounts(100, -500000, 3600), 0);
            return true;
        });

        suite->addTest("a nonsense calibration leaves the counts alone", []() {
            // Guarding the divide. Better to under-apply the correction than
            // to divide by zero on a config the operator can hand-edit.
            ASSERT_EQ(correctedDistanceCounts(1000, -5000, 0), 1000);
            ASSERT_EQ(correctedDistanceCounts(1000, -5000, -7), 1000);
            return true;
        });
        return suite;
    }
};

#endif // TEST_DISTANCE_ADJUST_H
