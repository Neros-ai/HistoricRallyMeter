#ifndef TONE_CADENCE_H
#define TONE_CADENCE_H

#include <string>
#include "arrow_tone.h"
#include "simple_tone.h"

// What the ahead/behind tone is doing right now. The box's speaker plays it
// (ToneGenerator::setCadence) and the phone is sent it (RB-WEB-02), so the
// two can never disagree. tone_ms 0 is silence; silence_ms 0 with tone_ms
// above 0 is a continuous tone (the simple tone's sustain).
struct ToneCadence {
    int tone_ms = 0;
    int silence_ms = 0;
    double freq_hz = 0.0;
    bool triangle = false;   // else sine

    bool sounding() const { return tone_ms > 0 && freq_hz > 0.0; }
};

// The box's tone decision, lifted out of ui_driver.cpp unchanged: silent when
// the tone is switched off, otherwise the simple (time-error-only) tone or the
// arrow-based one, whichever simple_tone_mode selects. The simple tone's state
// is only advanced when that tone is the one in use, as before.
ToneCadence decideToneCadence(bool tone_enabled, bool simple_tone_mode,
                              const ArrowToneResult& arrow,
                              SimpleToneState& simple_state,
                              double seconds_ahead_behind,
                              double stage_distance_m, bool past_stage_end);

// The cadence as the phone's telemetry field, e.g.
// {"tone_ms":700,"silence_ms":300,"freq_hz":1046.50,"wave":"sine"}.
// Anything not sounding is sent as all zeros, so the phone has one test.
std::string toneCadenceJson(const ToneCadence& cadence);

#endif // TONE_CADENCE_H
