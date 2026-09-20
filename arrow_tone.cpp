#include "arrow_tone.h"
#include "calculations.h"
#include <cmath>

namespace {
// The arrow count and cadence for one error figure. Shared by the arrows
// (fed the raw error) and the tone (fed the latched one), so the two can
// never drift apart in how they classify the same number.
struct Classified {
    int num_arrows;
    bool increase_speed;
};

Classified classify(double secondsAheadBehind, double target_kph_raw, bool unitsMph) {
    Classified out{0, false};
    double abs_seconds = std::abs(secondsAheadBehind);
    if (abs_seconds <= ARROW_TONE_ARROW_FLOOR_S) return out;

    double target_time_s = 500.0 / (target_kph_raw / 3.6);
    double adjusted_time_s = (secondsAheadBehind < 0)
        ? target_time_s - abs_seconds
        : target_time_s + abs_seconds;

    double speed_diff;
    if (adjusted_time_s > 0.1) {
        double needed_kph = (500.0 / adjusted_time_s) * 3.6;
        speed_diff = needed_kph - target_kph_raw;
    } else {
        speed_diff = (secondsAheadBehind < 0) ? 999.0 : -999.0;
    }

    if (unitsMph) {
        speed_diff = speed_diff * 0.621371;
    }

    double abs_diff = std::abs(speed_diff);
    if (abs_diff >= 10.0) {
        out.num_arrows = 3;
    } else if (abs_diff >= 3.0) {
        out.num_arrows = 2;
    } else if (abs_diff > 0) {
        out.num_arrows = 1;
    }
    out.increase_speed = (speed_diff > 0);
    return out;
}
}  // namespace

ArrowToneResult computeArrowBasedTone(ArrowToneState& state,
                                        double secondsAheadBehind,
                                        double targetSpeedCountsPerHour,
                                        long calibration,
                                        bool unitsMph,
                                        double stageDistanceMeters,
                                        bool pastStageEnd) {
    ArrowToneResult result{0, false, false, 0, 0, 0.0};

    double target_kph_raw = countsPerHourToKPH(targetSpeedCountsPerHour, calibration);
    if (target_kph_raw <= 0.0) {
        state.lastCommittedSeconds = 0.0;
        return result;
    }

    // The arrows: straight off the live error, exactly as before.
    Classified live = classify(secondsAheadBehind, target_kph_raw, unitsMph);
    result.num_arrows = live.num_arrows;
    result.increase_speed = live.increase_speed;

    bool in_tone_zone = (stageDistanceMeters >= 250.0) && !pastStageEnd;
    if (!in_tone_zone) {
        // As the simple tone does: a fresh zone starts with a fresh latch, so
        // a new stage is never judged against the last one's error.
        state.lastCommittedSeconds = 0.0;
        return result;
    }

    // Whether it sounds at all is one comparison on the LIVE error: at or above
    // ARROW_TONE_SOUND_THRESHOLD_S it sounds, below it there is silence. Same
    // point in both directions, no memory of how the error got there.
    //
    // The latch survives for the CADENCE only: it decides which of the three
    // rhythms plays, so small wobble cannot shuffle between them. While silent
    // it simply follows the live error, which keeps it armed with the right
    // value for the tick the tone starts on.
    if (std::abs(secondsAheadBehind) < ARROW_TONE_SOUND_THRESHOLD_S) {
        state.lastCommittedSeconds = secondsAheadBehind;
        return result;
    }
    if (std::abs(secondsAheadBehind - state.lastCommittedSeconds)
            > ARROW_TONE_UPDATE_THRESHOLD_S) {
        state.lastCommittedSeconds = secondsAheadBehind;
    }
    const double committed = state.lastCommittedSeconds;
    if (std::abs(committed) > 30.0) {
        return result;  // arrows/direction stay valid for the label
    }

    Classified latched = classify(committed, target_kph_raw, unitsMph);
    if (latched.num_arrows == 0) {
        return result;
    }

    result.tone_active = true;
    result.freq_hz = latched.increase_speed ? 1046.50 : 1396.91;
    if (latched.num_arrows >= 3) {
        result.tone_ms = 700; result.silence_ms = 300;
    } else if (latched.num_arrows == 2) {
        result.tone_ms = 500; result.silence_ms = 200;
    } else {
        result.tone_ms = 100; result.silence_ms = 100;
    }
    return result;
}
