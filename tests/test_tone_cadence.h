#ifndef TEST_TONE_CADENCE_H
#define TEST_TONE_CADENCE_H

#include "test_framework.h"
#include "../tone_cadence.h"
#include <string>

// RB-WEB-02: the ahead/behind tone decision the box's speaker and the phone
// both follow, and the telemetry field that carries it to the phone.
class TestToneCadence {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Tone Cadence");

        suite->addTest("tone switched off is silent and leaves the simple tone alone", []() {
            ArrowToneResult arrow{};
            arrow.tone_active = true; arrow.tone_ms = 500; arrow.silence_ms = 200; arrow.freq_hz = 1046.50;
            SimpleToneState simple;
            ToneCadence c = decideToneCadence(false, true, arrow, simple, -2.0, 1000.0, false);
            return !c.sounding() && simple.lastCommittedSeconds == 0.0;
        });

        suite->addTest("arrow mode plays the arrow tone's cadence, sine", []() {
            ArrowToneResult arrow{};
            arrow.tone_active = true; arrow.tone_ms = 500; arrow.silence_ms = 200; arrow.freq_hz = 1046.50;
            SimpleToneState simple;
            ToneCadence c = decideToneCadence(true, false, arrow, simple, -2.0, 1000.0, false);
            return c.tone_ms == 500 && c.silence_ms == 200 && c.freq_hz == 1046.50
                && !c.triangle && simple.lastCommittedSeconds == 0.0;
        });

        suite->addTest("arrow mode is silent when the arrow tone is", []() {
            ArrowToneResult arrow{};
            SimpleToneState simple;
            return !decideToneCadence(true, false, arrow, simple, -2.0, 1000.0, false).sounding();
        });

        suite->addTest("simple mode behind is a continuous triangle tone", []() {
            ArrowToneResult arrow{};
            SimpleToneState simple;
            ToneCadence c = decideToneCadence(true, true, arrow, simple, -2.0, 1000.0, false);
            return c.tone_ms == SIMPLE_TONE_SUSTAIN_MS && c.silence_ms == 0
                && c.triangle && c.freq_hz > 1800.0;
        });

        suite->addTest("simple mode ahead is a continuous sine tone", []() {
            ArrowToneResult arrow{};
            SimpleToneState simple;
            ToneCadence c = decideToneCadence(true, true, arrow, simple, 2.0, 1000.0, false);
            return c.sounding() && c.silence_ms == 0 && !c.triangle && c.freq_hz < 1600.1;
        });

        suite->addTest("telemetry field carries the cadence", []() {
            ToneCadence c;
            c.tone_ms = 700; c.silence_ms = 300; c.freq_hz = 1396.91;
            return toneCadenceJson(c)
                == "{\"tone_ms\":700,\"silence_ms\":300,\"freq_hz\":1396.91,\"wave\":\"sine\"}";
        });

        suite->addTest("telemetry field is all zeros when silent", []() {
            ToneCadence c;
            c.tone_ms = 500;  // no frequency: not sounding
            c.triangle = true;
            return toneCadenceJson(c)
                == "{\"tone_ms\":0,\"silence_ms\":0,\"freq_hz\":0.00,\"wave\":\"sine\"}";
        });

        // RB-WEB-06. Beep Assist plays to the box's own speaker, so the phone
        // is told each beep by sequence number instead.
        suite->addTest("a navigation beep carries its pair and gap", []() {
            BeepEvent b;
            b.seq = 4; b.freq_hz = 3200.0; b.duration_ms = 100; b.amplitude = 0.45;
            b.twice = true; b.gap_ms = 150;
            return beepEventJson(b)
                == "{\"seq\":4,\"freq_hz\":3200.00,\"ms\":100,\"amp\":0.45,"
                   "\"wave\":\"sine\",\"twice\":true,\"gap_ms\":150}";
        });

        suite->addTest("a timing beep is a single beep", []() {
            BeepEvent b;
            b.seq = 1; b.freq_hz = 3200.0; b.duration_ms = 250; b.amplitude = 0.45;
            return beepEventJson(b)
                == "{\"seq\":1,\"freq_hz\":3200.00,\"ms\":250,\"amp\":0.45,"
                   "\"wave\":\"sine\",\"twice\":false,\"gap_ms\":0}";
        });

        // Seq 0 is the "nothing has beeped this run" value a phone records
        // without playing, so it must be distinguishable from a real beep.
        suite->addTest("nothing beeped yet is sequence zero", []() {
            return beepEventJson(BeepEvent{})
                == "{\"seq\":0,\"freq_hz\":0.00,\"ms\":0,\"amp\":0.00,"
                   "\"wave\":\"sine\",\"twice\":false,\"gap_ms\":0}";
        });

        return suite;
    }
};

#endif // TEST_TONE_CADENCE_H
