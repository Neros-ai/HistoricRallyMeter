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

// A Beep Assist beep, published for the phone the same way the cadence above
// is. The box plays these straight to its own speaker
// (ToneGenerator::playBeep), which the phone never sees, so without this the
// phone stayed silent through every waypoint.
//
// A beep is an instant, not a state, and telemetry is a 10Hz snapshot that can
// drop a frame -- so it is carried as a sequence number rather than a flag the
// phone might sample between frames. The phone plays a beep when `seq` advances
// and otherwise leaves it alone; it must also record the first `seq` it ever
// sees WITHOUT playing, or a phone joining mid-rally announces the last
// waypoint the car already passed.
//
// `twice` is navigation mode's "bing bong": the box plays its second beep off a
// GTK timer, but the phone is told the gap so it can schedule both itself
// rather than depending on a second frame arriving.
struct BeepEvent {
    unsigned long seq = 0;   // 0 = nothing has beeped this run
    double freq_hz = 0.0;
    int duration_ms = 0;
    double amplitude = 0.0;
    bool triangle = false;   // else sine
    bool twice = false;
    int gap_ms = 0;          // start-to-start, meaningful only when twice
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

// Telemetry's "beep" object. seq 0 renders as a null beep, which the phone
// reads as "nothing has fired yet".
std::string beepEventJson(const BeepEvent& beep);

// The cadence as the phone's telemetry field, e.g.
// {"tone_ms":700,"silence_ms":300,"freq_hz":1046.50,"wave":"sine"}.
// Anything not sounding is sent as all zeros, so the phone has one test.
std::string toneCadenceJson(const ToneCadence& cadence);

#endif // TONE_CADENCE_H
