#ifndef ARROW_TONE_H
#define ARROW_TONE_H

// The existing ahead/behind tone: converts the raw time-error seconds into
// the speed change needed to close the gap over the next 500m (using the
// current segment's target speed), then maps that speed_diff to an arrow
// count (for the on-screen indicator) and a tone cadence/frequency.
//
// Extracted verbatim from ui_driver.cpp's tick-update logic so it is
// unit-testable without GTK, and so ui_driver.cpp does not need to inline
// this logic a second time when branching against simple_tone.h's
// alternative.
//
// num_arrows/increase_speed are computed (and valid) whenever the error is
// more than 0.1s and a target speed exists -- this drives the on-screen
// arrows regardless of which tone algorithm is currently selected for
// audio. tone_active/tone_ms/silence_ms/freq_hz are additionally gated by
// the 250m/stage-end/30s zone (same as the rest of this function) and are
// only meaningful when the caller is in arrow-tone audio mode.
// Runtime (non-persisted) state, exactly as SimpleToneState is for the other
// tone mode. Lives in AppData; the default-constructed value is the correct
// "nothing latched yet" start.
struct ArrowToneState {
    double lastCommittedSeconds = 0.0;
};

// The error has to move this far before the tone's decision follows it. Same
// value, and the same purpose, as the simple tone's latch.
constexpr double ARROW_TONE_UPDATE_THRESHOLD_S = 0.2;

struct ArrowToneResult {
    int num_arrows;         // 0-3; 0 = no correction needed or gated off
    bool increase_speed;    // true = speed up (behind), false = slow down (ahead); valid only if num_arrows > 0
    bool tone_active;       // true => sound at freq_hz with the given cadence
    int tone_ms;
    int silence_ms;
    double freq_hz;
};

// The ARROWS follow secondsAheadBehind directly, as they always have -- the
// on-screen indicator is unchanged by this.
//
// The TONE is decided from a latched copy instead (state.lastCommittedSeconds,
// moved only when the error shifts by more than
// ARROW_TONE_UPDATE_THRESHOLD_S). Every gate below is a hard edge on a figure
// that is recomputed every 10ms: the 0.1s floor, the 30s ceiling, and the
// 3/10 kph steps that pick the cadence. An error parked on any of them
// alternated on every tick, so the tone chattered -- loudest when the crew are
// driving WELL and sitting on the 0.1s floor. The gauge had the same fault and
// was given a deadband (RB-DRV-10); the simple tone has had this latch since
// it was written; the arrow tone had neither. Leaving the zone clears the
// latch, so a new stage starts fresh.
ArrowToneResult computeArrowBasedTone(ArrowToneState& state,
                                        double secondsAheadBehind,
                                        double targetSpeedCountsPerHour,
                                        long calibration,
                                        bool unitsMph,
                                        double stageDistanceMeters,
                                        bool pastStageEnd);

#endif // ARROW_TONE_H
