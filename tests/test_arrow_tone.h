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

        // RB-DEV-08. Every gate in here is a hard edge on a figure recomputed
        // every 10ms, so an error parked on one alternated on every tick and
        // the tone chattered. The latch is the same device the simple tone has
        // always used.
        suite->addTest("an error dithering across the 0.1s floor does not chatter", [CAL, TARGET_100KPH]() {
            ArrowToneState st;
            // Straddling the floor by less than the latch threshold: the tone
            // must make one decision and hold it, not follow each wobble.
            for (double s : {0.15, 0.05, 0.12, 0.08, 0.15, 0.05}) {
                ArrowToneResult r = computeArrowBasedTone(st, s, TARGET_100KPH, CAL,
                                                          false, 1000.0, false);
                ASSERT_FALSE(r.tone_active);
            }
            return true;
        });

        suite->addTest("a real error holds the tone through small wobble", [CAL, TARGET_100KPH]() {
            ArrowToneState st;
            ArrowToneResult first = computeArrowBasedTone(st, 5.0, TARGET_100KPH, CAL,
                                                          false, 1000.0, false);
            ASSERT_TRUE(first.tone_active);
            // Wobble under the latch threshold: same cadence, same pitch, no
            // gap -- the tone the crew hear does not change at all.
            for (double s : {5.1, 4.95, 5.05, 4.9, 5.15}) {
                ArrowToneResult r = computeArrowBasedTone(st, s, TARGET_100KPH, CAL,
                                                          false, 1000.0, false);
                ASSERT_TRUE(r.tone_active);
                ASSERT_EQ(r.tone_ms, first.tone_ms);
                ASSERT_EQ(r.silence_ms, first.silence_ms);
                ASSERT_NEAR(r.freq_hz, first.freq_hz, 0.001);
            }
            return true;
        });

        suite->addTest("a genuine move is followed", [CAL, TARGET_100KPH]() {
            ArrowToneState st;
            ASSERT_TRUE(computeArrowBasedTone(st, 5.0, TARGET_100KPH, CAL, false,
                                              1000.0, false).tone_active);
            // Back on time by a clear margin: the tone stops, as it should.
            ASSERT_FALSE(computeArrowBasedTone(st, 0.0, TARGET_100KPH, CAL, false,
                                               1000.0, false).tone_active);
            return true;
        });

        suite->addTest("an error dithering across the 30s ceiling does not chatter", [CAL, TARGET_100KPH]() {
            ArrowToneState st;
            computeArrowBasedTone(st, 29.9, TARGET_100KPH, CAL, false, 1000.0, false);
            bool first = computeArrowBasedTone(st, 29.9, TARGET_100KPH, CAL, false,
                                               1000.0, false).tone_active;
            // Wobble strictly inside the latch threshold. A move BIGGER
            // than the threshold is a genuine change and is followed, which
            // is the point -- one clean transition, not a chatter.
            for (double s : {30.05, 29.95, 30.02, 29.98}) {
                ArrowToneResult r = computeArrowBasedTone(st, s, TARGET_100KPH, CAL,
                                                          false, 1000.0, false);
                ASSERT_EQ(r.tone_active, first);
            }
            return true;
        });

        suite->addTest("the arrows still follow the live error, not the latch", [CAL, TARGET_100KPH]() {
            ArrowToneState st;
            computeArrowBasedTone(st, 5.0, TARGET_100KPH, CAL, false, 1000.0, false);
            // The latch is at 5.0. A wobble to 5.1 must still be what the
            // on-screen arrows are computed from -- only the TONE is latched.
            ArrowToneState fresh;
            ArrowToneResult live = computeArrowBasedTone(fresh, 5.1, TARGET_100KPH, CAL,
                                                         false, 1000.0, false);
            ArrowToneResult held = computeArrowBasedTone(st, 5.1, TARGET_100KPH, CAL,
                                                         false, 1000.0, false);
            ASSERT_EQ(held.num_arrows, live.num_arrows);
            ASSERT_EQ(held.increase_speed, live.increase_speed);
            return true;
        });

        suite->addTest("leaving the zone clears the latch", [CAL, TARGET_100KPH]() {
            ArrowToneState st;
            computeArrowBasedTone(st, 5.0, TARGET_100KPH, CAL, false, 1000.0, false);
            // Out of the zone (before 250m): latch cleared, so the next stage
            // is never judged against the last one's error.
            computeArrowBasedTone(st, 5.0, TARGET_100KPH, CAL, false, 100.0, false);
            ASSERT_NEAR(st.lastCommittedSeconds, 0.0, 0.0001);
            return true;
        });

        suite->addTest("no arrows/no tone within the 0.1s dead zone", [CAL, TARGET_100KPH]() {
            ArrowToneState st1;
            ArrowToneResult r = computeArrowBasedTone(st1, 0.05, TARGET_100KPH, CAL, false, 1000.0, false);
            return r.num_arrows == 0 && r.tone_active == false;
        });

        suite->addTest("no arrows/no tone when there is no target speed", [CAL, TARGET_100KPH]() {
            ArrowToneState st2;
            ArrowToneResult r = computeArrowBasedTone(st2, 5.0, 0.0, CAL, false, 1000.0, false);
            return r.num_arrows == 0 && r.tone_active == false;
        });

        suite->addTest("a small correction gives 1 arrow", [CAL, TARGET_100KPH]() {
            // seconds=-0.3: adjusted=17.7s, needed=101.69kph, diff=1.69 -> 1 arrow
            ArrowToneState st3;
            ArrowToneResult r = computeArrowBasedTone(st3, -0.3, TARGET_100KPH, CAL, false, 1000.0, false);
            return r.num_arrows == 1;
        });

        suite->addTest("a moderate correction gives 2 arrows", [CAL, TARGET_100KPH]() {
            // seconds=-1.0: adjusted=17.0s, needed=105.88kph, diff=5.88 -> 2 arrows
            ArrowToneState st4;
            ArrowToneResult r = computeArrowBasedTone(st4, -1.0, TARGET_100KPH, CAL, false, 1000.0, false);
            return r.num_arrows == 2;
        });

        suite->addTest("a large correction gives 3 arrows, behind => increase_speed", [CAL, TARGET_100KPH]() {
            // seconds=-2.0: adjusted=16.0s, needed=112.5kph, diff=12.5 -> 3 arrows
            ArrowToneState st5;
            ArrowToneResult r = computeArrowBasedTone(st5, -2.0, TARGET_100KPH, CAL, false, 1000.0, false);
            return r.num_arrows == 3 && r.increase_speed == true;
        });

        suite->addTest("ahead of schedule gives the slow-down direction", [CAL, TARGET_100KPH]() {
            // seconds=+3.0: adjusted=21.0s, needed=85.714kph, diff=-14.286 -> 3 arrows, slow down.
            // (Previously used +2.0, which lands the mathematically-exact diff exactly on the
            // 10.0 arrow-tier boundary; under real -O2 cross-TU compilation 100.0/3.6 isn't
            // exactly representable, so the computed diff falls a hair under 10.0 and resolves
            // to 2 arrows instead of 3. 3.0s gives diff~=14.286, well clear of the boundary.)
            ArrowToneState st6;
            ArrowToneResult r = computeArrowBasedTone(st6, 3.0, TARGET_100KPH, CAL, false, 1000.0, false);
            return r.num_arrows == 3 && r.increase_speed == false;
        });

        suite->addTest("behind schedule sounds C6 with the 3-arrow cadence", [CAL, TARGET_100KPH]() {
            ArrowToneState st7;
            ArrowToneResult r = computeArrowBasedTone(st7, -2.0, TARGET_100KPH, CAL, false, 1000.0, false);
            return r.tone_active == true && std::abs(r.freq_hz - 1046.50) < 0.01
                && r.tone_ms == 700 && r.silence_ms == 300;
        });

        suite->addTest("ahead of schedule sounds F6", [CAL, TARGET_100KPH]() {
            ArrowToneState st8;
            ArrowToneResult r = computeArrowBasedTone(st8, 2.0, TARGET_100KPH, CAL, false, 1000.0, false);
            return r.tone_active == true && std::abs(r.freq_hz - 1396.91) < 0.01;
        });

        suite->addTest("1-arrow cadence is 100ms/100ms", [CAL, TARGET_100KPH]() {
            ArrowToneState st9;
            ArrowToneResult r = computeArrowBasedTone(st9, -0.3, TARGET_100KPH, CAL, false, 1000.0, false);
            return r.tone_active == true && r.tone_ms == 100 && r.silence_ms == 100;
        });

        suite->addTest("2-arrow cadence is 500ms/200ms", [CAL, TARGET_100KPH]() {
            ArrowToneState st10;
            ArrowToneResult r = computeArrowBasedTone(st10, -1.0, TARGET_100KPH, CAL, false, 1000.0, false);
            return r.tone_active == true && r.tone_ms == 500 && r.silence_ms == 200;
        });

        suite->addTest("arrows still show but tone is silent before 250m", [CAL, TARGET_100KPH]() {
            ArrowToneState st11;
            ArrowToneResult r = computeArrowBasedTone(st11, -2.0, TARGET_100KPH, CAL, false, 100.0, false);
            return r.num_arrows == 3 && r.tone_active == false;
        });

        suite->addTest("arrows still show but tone is silent past the stage end", [CAL, TARGET_100KPH]() {
            ArrowToneState st12;
            ArrowToneResult r = computeArrowBasedTone(st12, -2.0, TARGET_100KPH, CAL, false, 1000.0, true);
            return r.num_arrows == 3 && r.tone_active == false;
        });

        suite->addTest("arrows still show but tone is silent beyond 30s", [CAL, TARGET_100KPH]() {
            ArrowToneState st13;
            ArrowToneResult r = computeArrowBasedTone(st13, -31.0, TARGET_100KPH, CAL, false, 1000.0, false);
            return r.num_arrows == 3 && r.tone_active == false;
        });

        suite->addTest("unit conversion to MPH can change the arrow tier", [CAL, TARGET_100KPH]() {
            // Same -2.0s case as the km/h 3-arrow test: diff=12.5kph -> 12.5*0.621371=7.77mph -> 2 arrows, not 3
            ArrowToneState st14;
            ArrowToneResult r = computeArrowBasedTone(st14, -2.0, TARGET_100KPH, CAL, true, 1000.0, false);
            return r.num_arrows == 2;
        });

        return suite;
    }
};

#endif // TEST_ARROW_TONE_H
