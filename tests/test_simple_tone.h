#ifndef TEST_SIMPLE_TONE_H
#define TEST_SIMPLE_TONE_H

#include "test_framework.h"
#include "../simple_tone.h"
#include "../tone_rung.h"
#include <algorithm>
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
            // The pitch is sampled on the 0.2s line (step 7 = 1.4s), not at 1.5s.
            SimpleToneResult r = updateSimpleTone(st, -1.5, 1000.0, false);
            double expected = BEHIND_MIN_HZ * std::pow(BEHIND_MAX_HZ / BEHIND_MIN_HZ, 1.4 / 3.0);
            return r.active == true && std::abs(r.freq_hz - expected) < 0.01;
        });

        suite->addTest("ahead pitch is exactly 800*sqrt(2) at 1.5s (t=0.5)", [AHEAD_MIN_HZ]() {
            SimpleToneState st;
            // The pitch is sampled on the 0.2s line (step 7 = 1.4s), not at 1.5s.
            SimpleToneResult r = updateSimpleTone(st, 1.5, 1000.0, false);
            double expected = AHEAD_MIN_HZ * std::pow(2.0, 1.4 / 3.0);
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

        suite->addTest("a move of 0.2s or less does not re-latch, holding the pitch", []() {
            SimpleToneState st;
            // Latch onto -0.30 (just past the band, behind direction).
            SimpleToneResult r1 = updateSimpleTone(st, -0.30, 1000.0, false);
            if (!(r1.active == true && r1.triangle_wave == true)) return false;
            double latched_freq = r1.freq_hz;
            // Raw is now -0.25: still outside the band, so still sounding, and
            // only 0.05s from the latch, so the PITCH must hold rather than
            // sliding with every 10ms wobble. That steadiness is what the
            // latch is for; it no longer has any say in whether the tone
            // sounds at all.
            SimpleToneResult r2 = updateSimpleTone(st, -0.25, 1000.0, false);
            return r2.active == true && std::abs(r2.freq_hz - latched_freq) < 0.01;
        });

        // The band is one edge on the live error, whichever way it is crossed.
        suite->addTest("0.20 sounds and 0.19 does not, in both directions", []() {
            SimpleToneState st;
            for (double e : {0.0, 0.10, 0.19}) {
                if (updateSimpleTone(st, e, 1000.0, false).active) return false;
            }
            if (!updateSimpleTone(st, 0.20, 1000.0, false).active) return false;
            // Coming back down: the same point, not a lower one.
            if (!updateSimpleTone(st, 0.20, 1000.0, false).active) return false;
            if (updateSimpleTone(st, 0.19, 1000.0, false).active) return false;
            // Behind schedule behaves identically.
            if (!updateSimpleTone(st, -0.20, 1000.0, false).active) return false;
            return updateSimpleTone(st, -0.19, 1000.0, false).active == false;
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

        // ---- Held-step pitch (shared rule with tone type 1, tone_rung.h) ----
        // Independent reference for the ramp, sampled at step `rung`'s line.
        auto refHz = [](bool behind, int rung) {
            const double t = std::min(0.2 * rung, 3.0) / 3.0;
            return behind ? 1800.0 * std::pow(3500.0 / 1800.0, t)
                          : 800.0 * std::pow(1600.0 / 800.0, t);
        };
        auto tick = [](SimpleToneState& st, double e) {
            return updateSimpleTone(st, e, 1000.0, false);
        };

        for (int side = 0; side < 2; side++) {
            const bool behind = (side == 0);
            const char* name = behind
                ? "growing error behind: pitch steps on every 0.2s line, in order, triangle"
                : "growing error ahead: pitch steps on every 0.2s line, in order, sine";
            suite->addTest(name, [refHz, tick, behind]() {
                const double sign = behind ? -1.0 : 1.0;
                for (int phase = 0; phase < 200; phase++) {
                    SimpleToneState st;
                    const double e0 = phase * (0.0005 / 200.0);
                    int prev = 0;
                    for (int i = 0; i <= 6800; i++) {
                        const double e = e0 + i * 0.0005;
                        SimpleToneResult r = tick(st, sign * e);
                        if (e < 0.2) { ASSERT_FALSE(r.active); continue; }
                        int rung = static_cast<int>(std::floor(e / 0.2 + 1e-9));
                        if (rung > 15) rung = 15;
                        ASSERT_TRUE(r.active);
                        ASSERT_NEAR(r.freq_hz, refHz(behind, rung), 1e-6);
                        ASSERT_EQ(r.triangle_wave, behind);
                        // Every step heard, none skipped, none repeated across a line.
                        ASSERT_TRUE(rung == prev || rung == prev + 1 || prev == 0);
                        prev = rung;
                    }
                    ASSERT_EQ(prev, 15);
                }
                return true;
            });
            suite->addTest(behind ? "shrinking error behind: each step held until 0.1s below its line"
                                  : "shrinking error ahead: each step held until 0.1s below its line",
                           [refHz, tick, behind]() {
                const double sign = behind ? -1.0 : 1.0;
                SimpleToneState st;
                tick(st, sign * 3.4);
                int rung = 15;
                for (double e = 3.4; e >= 0.0; e -= 0.0005) {
                    SimpleToneResult r = tick(st, sign * e);
                    if (e < 0.2) { ASSERT_FALSE(r.active); continue; }
                    while (rung > 1 && e < 0.2 * rung - 0.1 - 1e-9) rung--;
                    ASSERT_TRUE(r.active);
                    ASSERT_NEAR(r.freq_hz, refHz(behind, rung), 1e-6);
                    ASSERT_EQ(r.triangle_wave, behind);
                }
                ASSERT_EQ(rung, 1);
                return true;
            });
        }

        suite->addTest("sitting on a line does not flutter the pitch", [refHz, tick]() {
            for (double sign : {-1.0, 1.0}) {
                const bool behind = sign < 0.0;
                SimpleToneState st;
                for (double e = 0.0; e < 0.41; e += 0.01) tick(st, sign * e);
                for (int i = 0; i < 500; i++) {
                    double e = 0.40 + ((i % 3) - 1) * 0.05;        // 0.35, 0.40, 0.45
                    ASSERT_NEAR(tick(st, sign * e).freq_hz, refHz(behind, 2), 1e-6);
                }
                SimpleToneState st2;
                for (double e = 0.0; e <= 0.55; e += 0.01) tick(st2, sign * e);
                for (double e = 0.55; e >= 0.40; e -= 0.01) tick(st2, sign * e);
                for (int i = 0; i < 500; i++) {
                    double e = 0.40 + ((i % 3) - 1) * 0.05;
                    ASSERT_NEAR(tick(st2, sign * e).freq_hz, refHz(behind, 2), 1e-6);
                }
            }
            return true;
        });

        suite->addTest("a jump lands on the live step at once", [refHz, tick]() {
            SimpleToneState st;
            ASSERT_NEAR(tick(st, -0.9).freq_hz, refHz(true, 4), 1e-6);
            ASSERT_NEAR(tick(st, -0.25).freq_hz, refHz(true, 1), 1e-6);
            SimpleToneState sa;
            ASSERT_NEAR(tick(sa, 0.9).freq_hz, refHz(false, 4), 1e-6);
            ASSERT_NEAR(tick(sa, 0.25).freq_hz, refHz(false, 1), 1e-6);
            return true;
        });

        suite->addTest("ahead to behind in one tick takes the new side's pitch and wave", [refHz, tick]() {
            SimpleToneState st;
            SimpleToneResult a = tick(st, 0.5);
            ASSERT_NEAR(a.freq_hz, refHz(false, 2), 1e-6);
            ASSERT_FALSE(a.triangle_wave);
            SimpleToneResult b = tick(st, -0.5);
            ASSERT_NEAR(b.freq_hz, refHz(true, 2), 1e-6);
            ASSERT_TRUE(b.triangle_wave);
            // From a high step across to a low one: no carried-over step.
            SimpleToneState s2;
            tick(s2, 2.0);
            ASSERT_NEAR(tick(s2, -0.45).freq_hz, refHz(true, 2), 1e-6);
            SimpleToneState s3;
            tick(s3, -2.0);
            ASSERT_NEAR(tick(s3, 0.45).freq_hz, refHz(false, 2), 1e-6);
            return true;
        });

        suite->addTest("going silent by any gate clears the held step", [refHz]() {
            auto fresh = [](SimpleToneState& st) {
                updateSimpleTone(st, -1.1, 1000.0, false);   // step 5 held
                ASSERT_EQ(st.heldRung, 5);
                return true;
            };
            auto reenter = [](SimpleToneState& st) {
                return updateSimpleTone(st, -0.21, 1000.0, false);
            };
            SimpleToneState a; ASSERT_TRUE(fresh(a));
            updateSimpleTone(a, -0.1, 1000.0, false);                    // quiet band
            ASSERT_EQ(a.heldRung, 0);
            ASSERT_NEAR(reenter(a).freq_hz, refHz(true, 1), 1e-6);
            SimpleToneState c; ASSERT_TRUE(fresh(c));
            updateSimpleTone(c, -1.1, 100.0, false);                     // under 250 m
            ASSERT_EQ(c.heldRung, 0);
            ASSERT_NEAR(reenter(c).freq_hz, refHz(true, 1), 1e-6);
            SimpleToneState d; ASSERT_TRUE(fresh(d));
            updateSimpleTone(d, -1.1, 1000.0, true);                     // past the end
            ASSERT_EQ(d.heldRung, 0);
            ASSERT_NEAR(reenter(d).freq_hz, refHz(true, 1), 1e-6);
            SimpleToneState e; ASSERT_TRUE(fresh(e));
            updateSimpleTone(e, -31.0, 1000.0, false);                   // beyond 30 s
            ASSERT_EQ(e.heldRung, 0);
            ASSERT_NEAR(reenter(e).freq_hz, refHz(true, 1), 1e-6);
            return true;
        });

        suite->addTest("toneHeldRung: up on the line, held, down 0.1s below, clamped, from none", []() {
            ASSERT_EQ(toneHeldRung(0, 0.5), 2);        // from none: the live rung
            ASSERT_EQ(toneHeldRung(2, 0.6), 3);        // up exactly on the line
            ASSERT_EQ(toneHeldRung(2, 0.59), 2);       // just under it: stays
            ASSERT_EQ(toneHeldRung(3, 0.55), 3);       // down: held within 0.1 of the line
            ASSERT_EQ(toneHeldRung(3, 0.51), 3);
            ASSERT_EQ(toneHeldRung(3, 0.49), 2);       // 0.1 below the 0.6 line: steps down
            ASSERT_EQ(toneHeldRung(5, 0.25), 1);       // a big drop lands on the live rung
            ASSERT_EQ(toneHeldRung(1, 0.2), 1);        // bottom rung
            ASSERT_EQ(toneHeldRung(14, 3.0), 15);      // top
            ASSERT_EQ(toneHeldRung(15, 30.0), 15);     // clamped
            ASSERT_EQ(toneHeldRung(0, 99.0), 15);
            return true;
        });

        return suite;
    }
};

#endif // TEST_SIMPLE_TONE_H
