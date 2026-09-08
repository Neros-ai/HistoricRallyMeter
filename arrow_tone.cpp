#include "arrow_tone.h"
#include "calculations.h"
#include <cmath>

ArrowToneResult computeArrowBasedTone(double secondsAheadBehind,
                                        double targetSpeedCountsPerHour,
                                        long calibration,
                                        bool unitsMph,
                                        double stageDistanceMeters,
                                        bool pastStageEnd) {
    ArrowToneResult result{0, false, false, 0, 0, 0.0};

    double abs_seconds = std::abs(secondsAheadBehind);
    double target_kph_raw = countsPerHourToKPH(targetSpeedCountsPerHour, calibration);
    if (abs_seconds <= 0.1 || target_kph_raw <= 0.0) {
        return result;
    }

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
        result.num_arrows = 3;
    } else if (abs_diff >= 3.0) {
        result.num_arrows = 2;
    } else if (abs_diff > 0) {
        result.num_arrows = 1;
    }
    result.increase_speed = (speed_diff > 0);

    if (result.num_arrows == 0) {
        return result;
    }

    bool in_tone_zone = (stageDistanceMeters >= 250.0) && !pastStageEnd;
    if (!in_tone_zone || abs_seconds > 30.0) {
        return result;  // arrows/direction stay valid for the label; tone_active stays false
    }

    result.tone_active = true;
    result.freq_hz = result.increase_speed ? 1046.50 : 1396.91;
    if (result.num_arrows >= 3) {
        result.tone_ms = 700; result.silence_ms = 300;
    } else if (result.num_arrows == 2) {
        result.tone_ms = 500; result.silence_ms = 200;
    } else {
        result.tone_ms = 100; result.silence_ms = 100;
    }
    return result;
}
