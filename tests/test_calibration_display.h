#ifndef TEST_CALIBRATION_DISPLAY_H
#define TEST_CALIBRATION_DISPLAY_H

#include "test_framework.h"
#include "../calculations.h"
#include <cmath>
#include <string>

// Tests for the calibration screen's derived readouts. The figure being
// calibrated is pulses per kilometre; showing raw counter deltas without it
// leaves the operator no way to check the result against a known-good value.
class TestCalibrationDisplay {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Calibration Display");

        suite->addTest("pulses per km inverts the calibration", []() {
            // calibration is mm per 1000 counts, so counts per km = 1e9/cal.
            // 600000 mm/1000 counts -> 1666.67 counts per km.
            return std::abs(pulsesPerKm(600000) - 1666.6666) < 0.01;
        });

        suite->addTest("one metre per pulse gives 1000 pulses per km", []() {
            // 1 m per count = 1000 mm per count = 1000000 mm per 1000 counts.
            return std::abs(pulsesPerKm(1000000) - 1000.0) < 0.001;
        });

        suite->addTest("a zero calibration cannot divide by zero", []() {
            // An unwritten or corrupt config must not crash the screen.
            return pulsesPerKm(0) == 0.0 && pulsesPerKm(-5) == 0.0;
        });

        suite->addTest("calibration from pulses per km is the reciprocal of pulsesPerKm", []() {
            // Same formula both directions: 1e9 / x. 600000 mm/1000 counts
            // round-trips through pulsesPerKm and back.
            double pulses = pulsesPerKm(600000);
            return std::abs(calibrationFromPulsesPerKm(pulses) - 600000) <= 1;
        });

        suite->addTest("1000 pulses per km gives the same calibration as 1m per pulse", []() {
            // The escape hatch this replaces hardcoded exactly this value.
            return calibrationFromPulsesPerKm(1000.0) == 1000000;
        });

        suite->addTest("a zero or negative pulses/km cannot divide by zero", []() {
            return calibrationFromPulsesPerKm(0.0) == 0
                && calibrationFromPulsesPerKm(-5.0) == 0;
        });

        // RB-CAL-06: Prop/Wheel RPM on the calibration screen.
        suite->addTest("prop rpm is counter-1 pulses/s / pulses per turn x 60", []() {
            // 478 pulses in 2 s at 8 pulses a turn: 239 Hz -> 1792.5 rpm.
            return std::abs(propRpmFromCounts(10478, 10000, 2000, 8) - 1792.5) < 1e-9;
        });

        suite->addTest("prop rpm reads through a 32-bit counter wrap", []() {
            // The LS7866C is 32-bit: 0xFFFFFF00 -> 0x000000DE is 478 pulses,
            // not a four-billion-pulse jump backwards.
            return std::abs(propRpmFromCounts(0xDEULL, 0xFFFFFF00ULL, 2000, 8) - 1792.5) < 1e-9;
        });

        suite->addTest("prop rpm is invalid with no elapsed time", []() {
            // get10th() returns all zeros until ~1.3 s of history exists.
            return propRpmFromCounts(500, 500, 0, 8) < 0.0;
        });

        suite->addTest("prop rpm is invalid for a nonsense pulses per turn", []() {
            return propRpmFromCounts(10478, 10000, 2000, 0) < 0.0;
        });

        suite->addTest("pulses per turn accepts 1 to 64", []() {
            return validPropPulsesPerRev(1) && validPropPulsesPerRev(8)
                && validPropPulsesPerRev(64)
                && !validPropPulsesPerRev(0) && !validPropPulsesPerRev(65);
        });

        suite->addTest("prop rpm reading is whole rpm with no separator", []() {
            return propRpmReading(1792.5) == "1793"
                && propRpmReading(999.4) == "999"
                && propRpmReading(12345.0) == "12345";
        });

        suite->addTest("prop rpm reading shows dashes when there is no figure", []() {
            return propRpmReading(-1.0) == "---";
        });

        return suite;
    }
};

#endif // TEST_CALIBRATION_DISPLAY_H
