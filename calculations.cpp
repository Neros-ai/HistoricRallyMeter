#include "calculations.h"
#include <cmath>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <iomanip>
#include <sstream>

int64_t calculateDistanceCounts(const RallyState& state, uint64_t cntr1, uint64_t cntr2,
                                  uint64_t start1, uint64_t start2) {
    int64_t delta1 = static_cast<int64_t>(cntr1) - static_cast<int64_t>(start1);
    
    if (state.counters) {
        // Two wheel: average
        int64_t delta2 = static_cast<int64_t>(cntr2) - static_cast<int64_t>(start2);
        return (delta1 + delta2) / 2;
    } else {
        // One gearbox: just CNTR_1
        return delta1;
    }
}

// High precision: counts to meters
double countsToMeters(int64_t counts, long calibration) {
    // calibration = mm per 1000 counts
    // meters = counts * (calibration / 1000) / 1000 = counts * calibration / 1e6
    return (static_cast<double>(counts) * calibration) / 1e6;
}

long countsToCentimeters(int64_t counts, long calibration) {
    // meters = (counts * calibration) / 1000 / 1000
    // centimeters = (counts * calibration) / 1000 / 10
    return (counts * calibration) / 10000;
}

double countsPerHourToKPH(double counts_per_hour, long calibration) {
    // calibration = mm per 1000 counts
    // m/count = calibration / 1000000
    // meters/hour = counts_per_hour * calibration / 1000000
    // km/hour = meters/hour / 1000 = counts_per_hour * calibration / 1e9
    return (counts_per_hour * calibration) / 1e9;
}

double kphToCountsPerHour(double kph, long calibration) {
    // Inverse of countsPerHourToKPH
    // counts_per_hour = kph * 1e9 / calibration
    return (kph * 1e9) / calibration;
}

int64_t getRallyTime_ms(const RallyState& state) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    return ms + state.rallyTimeOffset_ms;
}

std::string formatTime(int64_t time_ms) {
    time_t seconds = time_ms / 1000;
    struct tm* tm = localtime(&seconds);
    char buf[20];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", tm->tm_hour, tm->tm_min, tm->tm_sec);
    return std::string(buf);
}

std::string formatDuration(int64_t duration_ms) {
    int64_t total_seconds = duration_ms / 1000;
    int tenths = (duration_ms % 1000) / 100;
    int hours = total_seconds / 3600;
    int minutes = (total_seconds % 3600) / 60;
    int seconds = total_seconds % 60;
    char buf[24];
    snprintf(buf, sizeof(buf), "%03d:%02d:%02d.%d", hours, minutes, seconds, tenths);
    return std::string(buf);
}

double calculateCurrentSpeed(const RallyState& state, const CounterPoll& current, 
                            const CounterPoll& tenth) {
    if (tenth.time_ms == 0 || current.time_ms == 0) {
        return -1.0;  // Invalid
    }
    
    int64_t time_diff_ms = current.time_ms - tenth.time_ms;
    if (time_diff_ms <= 0) {
        return -1.0;
    }
    
    int64_t count_diff = calculateDistanceCounts(state, current.cntr1, current.cntr2,
                                                  tenth.cntr1, tenth.cntr2);
    long cm_diff = countsToCentimeters(count_diff, state.calibration);
    
    // Speed in cm/s
    double speed_cm_per_s = (cm_diff * 1000.0) / time_diff_ms;
    
    // Convert to KPH or MPH
    if (state.units) {
        // MPH: cm/s * 3600 / 160934
        return (speed_cm_per_s * 3600.0) / 160934.0;
    } else {
        // KPH: cm/s * 3600 / 100000
        return (speed_cm_per_s * 3600.0) / 100000.0;
    }
}

double calculateAverageSpeed(const RallyState& state, int64_t start_time_ms, 
                            int64_t current_time_ms, int64_t count_diff) {
    int64_t time_diff_ms = current_time_ms - start_time_ms;
    if (time_diff_ms <= 0) {
        return 0.0;
    }
    
    long cm_diff = countsToCentimeters(count_diff, state.calibration);
    double speed_cm_per_s = (cm_diff * 1000.0) / time_diff_ms;
    
    if (state.units) {
        return (speed_cm_per_s * 3600.0) / 160934.0;  // MPH
    } else {
        return (speed_cm_per_s * 3600.0) / 100000.0;  // KPH
    }
}

double calculateAheadBehind(const RallyState& state, int64_t current_time_ms,
                          int64_t segment_start_time, double target_counts_per_hour,
                          int64_t actual_counts) {
    if (state.segment_current_number < 0 || target_counts_per_hour == 0.0) {
        return 0.0;
    }
    
    // High precision calculation
    double time_hours_since_segment = static_cast<double>(current_time_ms - segment_start_time) / 3600000.0;
    double ideal_counts = time_hours_since_segment * target_counts_per_hour;
    double diff = static_cast<double>(actual_counts) - ideal_counts;
    double counts_per_second = target_counts_per_hour / 3600.0;
    double seconds = diff / counts_per_second;
    return seconds;
}

double calculateIdealCountsFromStageStart(const RallyState& state, int64_t elapsed_ms) {
    if (state.segment_current_number < 0 || state.stage_segments.empty()) {
        return 0.0;
    }
    
    double ideal_counts = 0.0;
    double remaining_time_s = elapsed_ms / 1000.0;
    
    // Go through each segment up to and including current
    for (int i = 0; i <= state.segment_current_number && i < static_cast<int>(state.stage_segments.size()); i++) {
        const Segment& seg = state.stage_segments[i];
        
        if (seg.target_speed_counts_per_hour <= 0.0) {
            continue;  // Skip invalid segments
        }
        
        // Time to complete this segment at target speed (in seconds)
        // time = distance / speed = distance_counts / (counts_per_hour / 3600)
        double segment_time_s = seg.distance_counts * 3600.0 / seg.target_speed_counts_per_hour;
        
        if (i < state.segment_current_number) {
            // Completed segment - add full distance
            ideal_counts += seg.distance_counts;
            remaining_time_s -= segment_time_s;
        } else {
            // Current segment - use remaining time at this segment's speed
            // remaining_time_s can be negative if ahead (drove earlier segments faster
            // than target), which correctly reduces ideal_counts
            double partial_counts = (remaining_time_s / 3600.0) * seg.target_speed_counts_per_hour;
            ideal_counts += partial_counts;
        }
    }
    
    return ideal_counts;
}

double calculateAheadBehindFromStageStart(const RallyState& state, int64_t current_time_ms,
                                          int64_t actual_counts_from_stage_start) {
    if (state.segment_current_number < 0 || state.stage_segments.empty()) {
        return 0.0;
    }
    
    // Get elapsed time since stage (total) start
    int64_t elapsed_ms = current_time_ms - state.total_start_time_ms;
    if (elapsed_ms <= 0) {
        return 0.0;
    }
    
    // Calculate where we should ideally be
    double ideal_counts = calculateIdealCountsFromStageStart(state, elapsed_ms);
    
    // Difference: positive = ahead (traveled more than ideal), negative = behind
    double diff = static_cast<double>(actual_counts_from_stage_start) - ideal_counts;
    
    // Convert to seconds using current segment's target speed
    const Segment& current_seg = state.stage_segments[state.segment_current_number];
    if (current_seg.target_speed_counts_per_hour <= 0.0) {
        return 0.0;
    }
    
    double counts_per_second = current_seg.target_speed_counts_per_hour / 3600.0;
    double seconds = diff / counts_per_second;

    return seconds;
}

std::string formatDistanceGrouped(long meters) {
    bool negative = meters < 0;
    std::string num = std::to_string(negative ? -meters : meters);
    std::string result;
    int count = 0;
    for (int i = static_cast<int>(num.size()) - 1; i >= 0; i--) {
        if (count > 0 && count % 3 == 0) result = ',' + result;
        result = num[i] + result;
        count++;
    }
    if (negative) result = '-' + result;
    return result;
}

std::string formatDistanceAutoUnit(long meters, const char** unit_out) {
    if (meters > 999999 || meters < -999999) {
        *unit_out = "km";
        return formatDistanceGrouped(meters / 1000);
    }
    *unit_out = "m";
    return formatDistanceGrouped(meters);
}

std::string formatElapsedInterval(int64_t total_secs) {
    if (total_secs < 0) total_secs = 0;
    int64_t minutes = total_secs / 60;
    char buf[24];
    if (minutes <= 9999) {
        snprintf(buf, sizeof(buf), "%lld:%02lld",
                 static_cast<long long>(minutes),
                 static_cast<long long>(total_secs % 60));
    } else {
        int64_t hours = minutes / 60;
        if (hours <= 9999) {
            snprintf(buf, sizeof(buf), "%lld:%02lld",
                     static_cast<long long>(hours),
                     static_cast<long long>(minutes % 60));
        } else {
            snprintf(buf, sizeof(buf), "toolong");
        }
    }
    return std::string(buf);
}

long clampDistanceAdjust(long raw_cm, long proposed_adjust_cm) {
    if (raw_cm + proposed_adjust_cm < 0) return -raw_cm;
    return proposed_adjust_cm;
}

long adjustedDistanceMeters(long raw_cm, long adjust_cm) {
    long corrected_cm = raw_cm + adjust_cm;
    if (corrected_cm < 0) corrected_cm = 0;
    return corrected_cm / 100;
}

std::string stageSummary(const std::vector<Segment>& segments) {
    if (segments.empty()) return std::string("No stage");

    std::stringstream ss;
    for (size_t i = 0; i < segments.size(); i++) {
        if (i > 0) ss << ", ";
        ss << static_cast<long>(segments[i].distance_m) << "m@"
           << std::fixed << std::setprecision(2) << segments[i].target_speed_kph << "kph";
    }
    return ss.str();
}

uint64_t autoStartSecondsFromTargetMs(int64_t target_ms, int64_t epoch_ms) {
    int64_t delta_ms = target_ms - epoch_ms;
    if (delta_ms < 0) return 0;
    return static_cast<uint64_t>(delta_ms / 1000);
}

int64_t autoStartTargetMsFromSeconds(uint64_t seconds, int64_t epoch_ms) {
    return epoch_ms + static_cast<int64_t>(seconds) * 1000;
}

AutoStartArming autoStartArming(int64_t target_ms, int64_t epoch_ms,
                                bool early_departure) {
    AutoStartArming arming{};
    arming.rally_time_s = autoStartSecondsFromTargetMs(target_ms, epoch_ms);
    arming.early_departure = early_departure;
    arming.triggered = false;
    arming.zero_distance_now = early_departure;
    return arming;
}

AutoStartArming autoStartDisarmed() {
    return AutoStartArming{};
}

bool autoStartHoldsTimeError(uint64_t auto_start_rally_time_s,
                             bool auto_start_early_departure,
                             bool auto_start_triggered,
                             int64_t diff_ms) {
    if (auto_start_rally_time_s == 0) return false;
    if (!auto_start_early_departure) return false;
    if (auto_start_triggered) return false;
    return diff_ms > 0 && diff_ms <= 24LL * 3600 * 1000;
}

double averageSpeedForDisplay(double average_speed, bool hold_at_zero) {
    return hold_at_zero ? 0.0 : average_speed;
}

bool stageDistanceComplete(const std::vector<Segment>& segs, int64_t stage_counts) {
    if (segs.empty()) return true;
    double total = 0.0;
    for (const auto& seg : segs) total += seg.distance_counts;
    return static_cast<double>(stage_counts) >= total;
}
