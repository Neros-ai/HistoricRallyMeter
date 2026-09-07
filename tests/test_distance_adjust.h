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

        return suite;
    }
};

#endif // TEST_DISTANCE_ADJUST_H
