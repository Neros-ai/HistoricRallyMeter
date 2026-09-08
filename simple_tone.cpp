#include "simple_tone.h"
#include <cmath>

namespace {
constexpr double QUIET_BAND_S = 0.2;
constexpr double UPDATE_THRESHOLD_S = 0.2;
constexpr double RAMP_END_S = 3.0;    // pitch keeps rising/falling out to here...
constexpr double MAX_ERROR_S = 30.0;  // ...then plateaus until this giving-up point
constexpr double MIN_STAGE_DISTANCE_M = 250.0;
// Behind schedule (negative x): 1800 Hz at -3s up to 3500 Hz approaching 0.
constexpr double BEHIND_MIN_FREQ_HZ = 1800.0;
constexpr double BEHIND_MAX_FREQ_HZ = 3500.0;
// Ahead of schedule (positive x): 800 Hz approaching 0 up to 1600 Hz at +3s.
constexpr double AHEAD_MIN_FREQ_HZ = 800.0;
constexpr double AHEAD_MAX_FREQ_HZ = 1600.0;

// Each half is its own continuous exponential curve (constant frequency
// ratio per second of error), clamped to its endpoint beyond +/-3s. Both
// halves rise with magnitude (louder/higher pitch = worse error): behind
// climbs 1800 Hz near zero -> 3500 Hz at -3s; ahead climbs 800 Hz near
// zero -> 1600 Hz at +3s. This does NOT make the two ranges' near-zero
// ends adjacent (3500 vs 800, a ~2700Hz jump across the silent band) --
// that was the accepted trade-off for keeping both sides' urgency
// direction intuitive rather than flipping one of them for adjacency.
double rampFrequency(double committedSeconds) {
    if (committedSeconds < 0.0) {
        double x = committedSeconds;
        if (x < -RAMP_END_S) x = -RAMP_END_S;
        double t = -x / RAMP_END_S;  // 0 at 0, 1 at -3s
        return BEHIND_MIN_FREQ_HZ * std::pow(BEHIND_MAX_FREQ_HZ / BEHIND_MIN_FREQ_HZ, t);
    }
    double x = committedSeconds;
    if (x > RAMP_END_S) x = RAMP_END_S;
    double t = x / RAMP_END_S;  // 0 at 0, 1 at +3s
    return AHEAD_MIN_FREQ_HZ * std::pow(AHEAD_MAX_FREQ_HZ / AHEAD_MIN_FREQ_HZ, t);
}
}  // namespace

SimpleToneResult updateSimpleTone(SimpleToneState& state,
                                    double secondsAheadBehind,
                                    double stageDistanceMeters,
                                    bool pastStageEnd) {
    bool in_zone = (stageDistanceMeters >= MIN_STAGE_DISTANCE_M) && !pastStageEnd;
    if (!in_zone) {
        state.lastCommittedSeconds = 0.0;
        return {false, 0.0, false};
    }

    if (std::abs(secondsAheadBehind - state.lastCommittedSeconds) > UPDATE_THRESHOLD_S) {
        state.lastCommittedSeconds = secondsAheadBehind;
    }

    double committed_abs = std::abs(state.lastCommittedSeconds);
    if (committed_abs <= QUIET_BAND_S || committed_abs > MAX_ERROR_S) {
        return {false, 0.0, false};
    }

    double freq = rampFrequency(state.lastCommittedSeconds);
    bool triangle = (state.lastCommittedSeconds < 0.0);
    return {true, freq, triangle};
}
