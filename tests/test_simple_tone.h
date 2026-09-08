#ifndef TEST_SIMPLE_TONE_H
#define TEST_SIMPLE_TONE_H

#include "test_framework.h"
#include "../simple_tone.h"
#include <cmath>

// Tests for the alternative, time-error-only ahead/behind tone: silent
// within +/-0.2s, otherwise a tone whose pitch climbs with the error's
// magnitude from the 0.2s edge out to the 3s ramp end, then plateaus from
// 3s out to the 30s giving-up point. Direction (sign) picks a
// non-overlapping pitch range and waveform: behind schedule is a TRIANGLE
// wave from 1800 Hz to 3500 Hz; ahead of schedule is a SINE wave from
// 800 Hz to 1600 Hz. The module only re-latches (and so only re-evaluates
// pitch/waveform) when the error moves more than 0.2s from the last
// latched value.
class TestSimpleTone {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Simple Tone Mode");
        constexpr double RAMP_END_S = 3.0;
        constexpr double BEHIND_MIN_HZ = 1800.0;
        constexpr double BEHIND_MAX_HZ = 3500.0;
        constexpr double AHEAD_MIN_HZ = 800.0;
        constexpr double AHEAD_MAX_HZ = 1600.0;

        suite->addTest("silent within the +/-0.2s quiet band", []() {
            SimpleToneState st;
            SimpleToneResult r = updateSimpleTone(st, 0.15, 500.0, false);
            return r.active == false;
        });

        suite->addTest("silent before 250m into the stage even with a large error", []() {
            SimpleToneState st;
            SimpleToneResult r = updateSimpleTone(st, 5.0, 100.0, false);
            return r.active == false;
        });

        suite->addTest("silent once past the stage end", []() {
            SimpleToneState st;
            SimpleToneResult r = updateSimpleTone(st, 5.0, 1000.0, true);
            return r.active == false;
        });

        suite->addTest("silent beyond +/-30s", []() {
            SimpleToneState st;
            SimpleToneResult r = updateSimpleTone(st, 31.0, 1000.0, false);
            return r.active == false;
        });

        suite->addTest("behind schedule starts near 1800 Hz just past the quiet band", [BEHIND_MIN_HZ, BEHIND_MAX_HZ]() {
            SimpleToneState st;
            SimpleToneResult r = updateSimpleTone(st, -0.21, 1000.0, false);
            return r.active == true
                && r.freq_hz > BEHIND_MIN_HZ && r.freq_hz < BEHIND_MAX_HZ
                && (r.freq_hz - BEHIND_MIN_HZ) < 150.0;
        });

        suite->addTest("ahead of schedule starts near 800 Hz just past the quiet band", [AHEAD_MIN_HZ, AHEAD_MAX_HZ]() {
            SimpleToneState st;
            SimpleToneResult r = updateSimpleTone(st, 0.21, 1000.0, false);
            return r.active == true
                && r.freq_hz > AHEAD_MIN_HZ && r.freq_hz < AHEAD_MAX_HZ
                && (r.freq_hz - AHEAD_MIN_HZ) < 100.0;
        });

        suite->addTest("behind schedule reaches exactly 3500 Hz at the 3s ramp end", [BEHIND_MAX_HZ, RAMP_END_S]() {
            SimpleToneState st;
            SimpleToneResult r = updateSimpleTone(st, -RAMP_END_S, 1000.0, false);
            return r.active == true && std::abs(r.freq_hz - BEHIND_MAX_HZ) < 0.01;
        });

        suite->addTest("ahead of schedule reaches exactly 1600 Hz at the 3s ramp end", [AHEAD_MAX_HZ, RAMP_END_S]() {
            SimpleToneState st;
            SimpleToneResult r = updateSimpleTone(st, RAMP_END_S, 1000.0, false);
            return r.active == true && std::abs(r.freq_hz - AHEAD_MAX_HZ) < 0.01;
        });

        suite->addTest("behind pitch is the exact geometric midpoint at 1.5s (t=0.5)", [BEHIND_MIN_HZ, BEHIND_MAX_HZ]() {
            SimpleToneState st;
            SimpleToneResult r = updateSimpleTone(st, -1.5, 1000.0, false);
            double expected = BEHIND_MIN_HZ * std::pow(BEHIND_MAX_HZ / BEHIND_MIN_HZ, 0.5);
            return r.active == true && std::abs(r.freq_hz - expected) < 0.01;
        });

        suite->addTest("ahead pitch is exactly 800*sqrt(2) at 1.5s (t=0.5)", [AHEAD_MIN_HZ]() {
            SimpleToneState st;
            SimpleToneResult r = updateSimpleTone(st, 1.5, 1000.0, false);
            double expected = AHEAD_MIN_HZ * std::sqrt(2.0);
            return r.active == true && std::abs(r.freq_hz - expected) < 0.01;
        });

        suite->addTest("pitch plateaus beyond the 3s ramp end: same frequency at 5s and 25s", []() {
            SimpleToneState st1;
            SimpleToneResult r1 = updateSimpleTone(st1, -5.0, 1000.0, false);
            SimpleToneState st2;
            SimpleToneResult r2 = updateSimpleTone(st2, -25.0, 1000.0, false);
            return r1.active && r2.active && std::abs(r1.freq_hz - r2.freq_hz) < 0.01;
        });

        suite->addTest("pitch rises monotonically with magnitude before the plateau", []() {
            SimpleToneState st1;
            SimpleToneResult r1 = updateSimpleTone(st1, -0.5, 1000.0, false);
            SimpleToneState st2;
            SimpleToneResult r2 = updateSimpleTone(st2, -1.5, 1000.0, false);
            SimpleToneState st3;
            SimpleToneResult r3 = updateSimpleTone(st3, -2.9, 1000.0, false);
            return r1.freq_hz < r2.freq_hz && r2.freq_hz < r3.freq_hz;
        });

        suite->addTest("behind and ahead pitch ranges never overlap", [BEHIND_MIN_HZ, AHEAD_MAX_HZ]() {
            // The top of the ahead range must stay below the bottom of the
            // behind range, so direction is always distinguishable by ear
            // (and by waveform shape) regardless of magnitude.
            return AHEAD_MAX_HZ < BEHIND_MIN_HZ;
        });

        suite->addTest("behind schedule sounds a triangle wave, ahead sounds a sine wave", []() {
            SimpleToneState st1;
            SimpleToneResult behind = updateSimpleTone(st1, -1.0, 1000.0, false);
            SimpleToneState st2;
            SimpleToneResult ahead = updateSimpleTone(st2, 1.0, 1000.0, false);
            return behind.active && ahead.active
                && behind.triangle_wave == true
                && ahead.triangle_wave == false;
        });

        suite->addTest("a move of 0.2s or less does not re-latch, holding the old tone", []() {
            SimpleToneState st;
            // Latch onto -0.30 (just past the band, behind direction).
            SimpleToneResult r1 = updateSimpleTone(st, -0.30, 1000.0, false);
            if (!(r1.active == true && r1.triangle_wave == true)) return false;
            double latched_freq = r1.freq_hz;
            // Raw is now -0.15 -- which is INSIDE the quiet band on its own
            // -- but only 0.15s away from the -0.30 latch, so the latch
            // (and its pitch) must hold rather than re-evaluating at -0.15
            // (which alone would be silent).
            SimpleToneResult r2 = updateSimpleTone(st, -0.15, 1000.0, false);
            return r2.active == true && std::abs(r2.freq_hz - latched_freq) < 0.01;
        });

        suite->addTest("a move of more than 0.2s re-latches, even crossing to silence", []() {
            SimpleToneState st;
            SimpleToneResult r1 = updateSimpleTone(st, -0.30, 1000.0, false);
            if (!(r1.active == true)) return false;
            // 0.35s away from the -0.30 latch -- must re-latch onto +0.05,
            // which is inside the quiet band, so the tone goes silent.
            SimpleToneResult r2 = updateSimpleTone(st, 0.05, 1000.0, false);
            return r2.active == false;
        });

        suite->addTest("leaving the zone resets the latch for the next entry", []() {
            SimpleToneState st;
            SimpleToneResult r1 = updateSimpleTone(st, -5.0, 1000.0, false);
            if (!(r1.active == true)) return false;
            // Leave the zone (before 250m of a new stage, say).
            SimpleToneResult r2 = updateSimpleTone(st, -5.0, 100.0, false);
            if (!(r2.active == false)) return false;
            // Re-enter with a small error that must NOT trigger just
            // because a large value was latched before leaving.
            SimpleToneResult r3 = updateSimpleTone(st, 0.15, 1000.0, false);
            return r3.active == false;
        });

        return suite;
    }
};

#endif // TEST_SIMPLE_TONE_H
