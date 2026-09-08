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

        suite->addTest("readout line carries distance and both sensors", []() {
            // Pulses/KM dropped from this line -- it's shown on its own row
            // elsewhere on the screen ("Current Calibration ... pulses/KM"),
            // repeating it here just pushed the line width past the
            // screen's budget for no benefit.
            std::string line = calibrationReadoutLine(1000, 3236, 3236, 3236);
            return line == "Device distance: 1000m. Pulses 3236 "
                           "S1=3236 S2=3236";
        });

        suite->addTest("readout does not group long distances with commas", []() {
            std::string line = calibrationReadoutLine(1234567, 10, 10, 10);
            return line.find("1234567m") != std::string::npos
                && line.find(',') == std::string::npos;
        });

        suite->addTest("readout shows the two sensors separately", []() {
            // The whole point of the breakdown is spotting one wheel sensor
            // disagreeing with the other.
            std::string line = calibrationReadoutLine(500, 1500, 1400, 1600);
            return line.find("S1=1400 S2=1600") != std::string::npos;
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

        return suite;
    }
};

#endif // TEST_CALIBRATION_DISPLAY_H
