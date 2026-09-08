#ifndef TEST_ARROW_TONE_H
#define TEST_ARROW_TONE_H

#include "test_framework.h"
#include "../arrow_tone.h"

// Tests for the existing speed/arrow-based ahead/behind tone, extracted
// out of ui_driver.cpp into a pure function. calibration=1e9 makes
// countsPerHourToKPH an identity function, so targetSpeedCountsPerHour=100
// means exactly 100 km/h and target_time_s (500m at 100kph) is 18.0s.
class TestArrowTone {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Arrow-Based Tone");
        constexpr long CAL = 1000000000;
        constexpr double TARGET_100KPH = 100.0;

        suite->addTest("no arrows/no tone within the 0.1s dead zone", [CAL, TARGET_100KPH]() {
            ArrowToneResult r = computeArrowBasedTone(0.05, TARGET_100KPH, CAL, false, 1000.0, false);
            return r.num_arrows == 0 && r.tone_active == false;
        });

        suite->addTest("no arrows/no tone when there is no target speed", [CAL, TARGET_100KPH]() {
            ArrowToneResult r = computeArrowBasedTone(5.0, 0.0, CAL, false, 1000.0, false);
            return r.num_arrows == 0 && r.tone_active == false;
        });

        suite->addTest("a small correction gives 1 arrow", [CAL, TARGET_100KPH]() {
            // seconds=-0.3: adjusted=17.7s, needed=101.69kph, diff=1.69 -> 1 arrow
            ArrowToneResult r = computeArrowBasedTone(-0.3, TARGET_100KPH, CAL, false, 1000.0, false);
            return r.num_arrows == 1;
        });

        suite->addTest("a moderate correction gives 2 arrows", [CAL, TARGET_100KPH]() {
            // seconds=-1.0: adjusted=17.0s, needed=105.88kph, diff=5.88 -> 2 arrows
            ArrowToneResult r = computeArrowBasedTone(-1.0, TARGET_100KPH, CAL, false, 1000.0, false);
            return r.num_arrows == 2;
        });

        suite->addTest("a large correction gives 3 arrows, behind => increase_speed", [CAL, TARGET_100KPH]() {
            // seconds=-2.0: adjusted=16.0s, needed=112.5kph, diff=12.5 -> 3 arrows
            ArrowToneResult r = computeArrowBasedTone(-2.0, TARGET_100KPH, CAL, false, 1000.0, false);
            return r.num_arrows == 3 && r.increase_speed == true;
        });

        suite->addTest("ahead of schedule gives the slow-down direction", [CAL, TARGET_100KPH]() {
            // seconds=+3.0: adjusted=21.0s, needed=85.714kph, diff=-14.286 -> 3 arrows, slow down.
            // (Previously used +2.0, which lands the mathematically-exact diff exactly on the
            // 10.0 arrow-tier boundary; under real -O2 cross-TU compilation 100.0/3.6 isn't
            // exactly representable, so the computed diff falls a hair under 10.0 and resolves
            // to 2 arrows instead of 3. 3.0s gives diff~=14.286, well clear of the boundary.)
            ArrowToneResult r = computeArrowBasedTone(3.0, TARGET_100KPH, CAL, false, 1000.0, false);
            return r.num_arrows == 3 && r.increase_speed == false;
        });

        suite->addTest("behind schedule sounds C6 with the 3-arrow cadence", [CAL, TARGET_100KPH]() {
            ArrowToneResult r = computeArrowBasedTone(-2.0, TARGET_100KPH, CAL, false, 1000.0, false);
            return r.tone_active == true && std::abs(r.freq_hz - 1046.50) < 0.01
                && r.tone_ms == 700 && r.silence_ms == 300;
        });

        suite->addTest("ahead of schedule sounds F6", [CAL, TARGET_100KPH]() {
            ArrowToneResult r = computeArrowBasedTone(2.0, TARGET_100KPH, CAL, false, 1000.0, false);
            return r.tone_active == true && std::abs(r.freq_hz - 1396.91) < 0.01;
        });

        suite->addTest("1-arrow cadence is 100ms/100ms", [CAL, TARGET_100KPH]() {
            ArrowToneResult r = computeArrowBasedTone(-0.3, TARGET_100KPH, CAL, false, 1000.0, false);
            return r.tone_active == true && r.tone_ms == 100 && r.silence_ms == 100;
        });

        suite->addTest("2-arrow cadence is 500ms/200ms", [CAL, TARGET_100KPH]() {
            ArrowToneResult r = computeArrowBasedTone(-1.0, TARGET_100KPH, CAL, false, 1000.0, false);
            return r.tone_active == true && r.tone_ms == 500 && r.silence_ms == 200;
        });

        suite->addTest("arrows still show but tone is silent before 250m", [CAL, TARGET_100KPH]() {
            ArrowToneResult r = computeArrowBasedTone(-2.0, TARGET_100KPH, CAL, false, 100.0, false);
            return r.num_arrows == 3 && r.tone_active == false;
        });

        suite->addTest("arrows still show but tone is silent past the stage end", [CAL, TARGET_100KPH]() {
            ArrowToneResult r = computeArrowBasedTone(-2.0, TARGET_100KPH, CAL, false, 1000.0, true);
            return r.num_arrows == 3 && r.tone_active == false;
        });

        suite->addTest("arrows still show but tone is silent beyond 30s", [CAL, TARGET_100KPH]() {
            ArrowToneResult r = computeArrowBasedTone(-31.0, TARGET_100KPH, CAL, false, 1000.0, false);
            return r.num_arrows == 3 && r.tone_active == false;
        });

        suite->addTest("unit conversion to MPH can change the arrow tier", [CAL, TARGET_100KPH]() {
            // Same -2.0s case as the km/h 3-arrow test: diff=12.5kph -> 12.5*0.621371=7.77mph -> 2 arrows, not 3
            ArrowToneResult r = computeArrowBasedTone(-2.0, TARGET_100KPH, CAL, true, 1000.0, false);
            return r.num_arrows == 2;
        });

        return suite;
    }
};

#endif // TEST_ARROW_TONE_H
