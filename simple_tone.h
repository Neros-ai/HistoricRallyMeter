#ifndef SIMPLE_TONE_H
#define SIMPLE_TONE_H

// Runtime (non-persisted) state for the alternative time-error-only tone
// decision logic. Lives in AppData; reset each app run (default-constructed
// value is the correct "nothing latched yet" starting point).
struct SimpleToneState {
    double lastCommittedSeconds = 0.0;
};

struct SimpleToneResult {
    bool active;         // true => sound at freq_hz; false => silence
    double freq_hz;       // meaningful only when active
    bool triangle_wave;   // true => triangle wave (behind), false => sine (ahead); meaningful only when active
};

// Passed as ToneGenerator::setCadence()'s tone_ms when active, with
// silence_ms=0, to get a continuous tone out of the existing pulsed-cadence
// engine unmodified: the on/off toggle it runs internally never actually
// fires within a realistic session, so it reads as fully sustained.
constexpr int SIMPLE_TONE_SUSTAIN_MS = 3600000;  // 1 hour

// Decides whether the alternative ahead/behind tone should sound, and at
// what frequency, using only the raw time-error seconds -- no speed or
// recovery-distance calculation (contrast with the existing tone,
// ui_driver.cpp:578-650).
//
// Zone gating matches the existing tone: silent before 250m into the stage
// or once past the stage's last segment (stageDistanceMeters/pastStageEnd,
// computed by the caller exactly as the existing tone block already does).
//
// Within the zone, the module maintains one latched value,
// state.lastCommittedSeconds. Each call, if the new secondsAheadBehind
// differs from the latch by more than 0.2s, the latch moves to the new
// value; otherwise it is left untouched. All output (silence vs. tone,
// direction, and pitch) is decided from the LATCH, not the raw input -- so
// small jitter around a boundary (the +/-0.2s quiet band, or the +/-30s
// giving-up point) does not chatter the tone on and off; only a genuine
// >0.2s move does. This is what makes the tone "constant": once latched,
// nothing changes until the error has moved a clear amount.
//
// Behind schedule (negative latch) is a TRIANGLE wave rising from
// 1800 Hz (approaching 0) toward 3500 Hz (at -3s); ahead of schedule
// (positive latch) is a SINE wave rising from 800 Hz (approaching 0)
// toward 1600 Hz (at +3s). Both halves rise with magnitude (worse error
// = higher pitch), which means their near-zero ends are NOT adjacent
// (3500 Hz vs 800 Hz -- a ~2700Hz jump across the silent band) -- that
// was a deliberate trade-off to keep the urgency direction intuitive on
// both sides rather than inverting one for a smoother crossing. Each
// half is its own continuous exponential curve (constant frequency
// ratio per second of error, so equal steps in the error always sound
// like equal-sized pitch jumps), and both waveform and frequency are
// always silent inside the +/-0.2s quiet band around zero. Beyond +/-3s
// the pitch plateaus at each half's top end (3500/1600 Hz) out to the
// +/-30s giving-up point -- so the tone keeps climbing as the error
// gets worse out to a reasonable point, without continuing to escalate
// all the way out to 30s.
//
// Leaving the zone resets the latch to 0.0, so re-entering (e.g. a new
// stage) starts fresh rather than comparing against a stale value from the
// previous stage.
SimpleToneResult updateSimpleTone(SimpleToneState& state,
                                    double secondsAheadBehind,
                                    double stageDistanceMeters,
                                    bool pastStageEnd);

#endif // SIMPLE_TONE_H
