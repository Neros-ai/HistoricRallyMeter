#include "web_telemetry.h"
#include "calculations.h"
#include "rally_types.h"
#include "counter_poller.h"
#include "rally_state.h"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <iomanip>

NextPrevState computeNextPrevState(AppData* data) {
    NextPrevState out;
    if (!data || !data->state || !data->poller) return out;

    if (data->state->segment_current_number < 0 ||
        data->state->segment_current_number >= static_cast<long>(data->state->segments.size())) {
        return out;
    }

    auto current_poll = data->poller->getMostRecent();
    const Segment& cur_seg = data->state->segments[data->state->segment_current_number];
    int64_t seg_count_diff = calculateDistanceCounts(*data->state,
        current_poll.cntr1, current_poll.cntr2,
        data->state->segment_start_cntr1, data->state->segment_start_cntr2);
    int64_t remaining_counts = cur_seg.distance_counts - seg_count_diff;
    long remaining_m = countsToCentimeters(remaining_counts, data->state->calibration) / 100;
    long travelled_m = countsToCentimeters(seg_count_diff, data->state->calibration) / 100;

    long next_seg_idx = data->state->segment_current_number + 1;
    bool near_end = (remaining_m >= 0 && remaining_m <= 500) &&
                    (next_seg_idx < static_cast<long>(data->state->segments.size()));
    bool near_start = (travelled_m >= 0 && travelled_m <= 500) &&
                      (data->state->segment_current_number > 0);

    if (near_end) {
        out.label = "next";
        out.enabled = true;
    } else if (near_start) {
        out.label = "prev";
        out.enabled = true;
    }
    return out;
}

static double displayKph(AppData* data, double kph) {
    if (data->state->units) return kph * 0.621371;
    return kph;
}

std::string buildTelemetryJson(AppData* data) {
    auto current_poll = data->poller->getMostRecent();
    auto tenth_poll = data->poller->get10th();
    int64_t current_time_ms = getRallyTime_ms(*data->state);

    int64_t trip_count_diff = calculateDistanceCounts(*data->state,
        current_poll.cntr1, current_poll.cntr2,
        data->state->trip_start_cntr1, data->state->trip_start_cntr2);
    long trip_m = adjustedDistanceMeters(
        countsToCentimeters(trip_count_diff, data->state->calibration),
        data->state->trip_distance_adjust_cm);
    double trip_avg = calculateAverageSpeed(*data->state,
        data->state->trip_start_time_ms, current_time_ms, trip_count_diff,
        data->state->trip_distance_adjust_cm);

    int64_t total_count_diff = calculateDistanceCounts(*data->state,
        current_poll.cntr1, current_poll.cntr2,
        data->state->total_start_cntr1, data->state->total_start_cntr2);
    long total_m = adjustedDistanceMeters(
        countsToCentimeters(total_count_diff, data->state->calibration),
        data->state->total_distance_adjust_cm);
    double total_avg = calculateAverageSpeed(*data->state,
        data->state->total_start_time_ms, current_time_ms, total_count_diff,
        data->state->total_distance_adjust_cm);

    double cur_speed = calculateCurrentSpeed(*data->state, current_poll, tenth_poll);
    if (cur_speed < 0) cur_speed = 0.0;

    double target_kph = 0.0;
    double ahead_behind_s = 0.0;
    if (data->state->segment_current_number >= 0 &&
        data->state->segment_current_number < static_cast<long>(data->state->segments.size())) {
        const Segment& seg = data->state->segments[data->state->segment_current_number];
        target_kph = countsPerHourToKPH(seg.target_speed_counts_per_hour, data->state->calibration);
        ahead_behind_s = calculateAheadBehindFromStageStart(*data->state, current_time_ms, total_count_diff);
        ahead_behind_s += data->state->ahead_behind_zero_offset_ms / 1000.0;
    }

    NextPrevState np = computeNextPrevState(data);
    std::string rally_clock = formatTime(current_time_ms);

    char buf[1024];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"telemetry\","
        "\"rally_clock\":\"%s\","
        "\"trip_m\":%ld,"
        "\"total_m\":%ld,"
        "\"cur_kph\":%.1f,"
        "\"trip_avg_kph\":%.1f,"
        "\"total_avg_kph\":%.1f,"
        "\"target_kph\":%.1f,"
        "\"ahead_behind_s\":%.1f,"
        "\"segment_number\":%ld,"
        "\"segment_count\":%zu,"
        "\"next_prev_label\":\"%s\","
        "\"next_prev_enabled\":%s,"
        "\"units\":\"%s\"}",
        rally_clock.c_str(),
        trip_m,
        total_m,
        displayKph(data, cur_speed),
        displayKph(data, trip_avg),
        displayKph(data, total_avg),
        displayKph(data, target_kph),
        ahead_behind_s,
        data->state->segment_current_number >= 0 ? data->state->segment_current_number + 1 : 0,
        data->state->segments.size(),
        np.label,
        np.enabled ? "true" : "false",
        data->state->units ? "mph" : "kph");
    return buf;
}

std::string buildStateJson(AppData* data) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "{\"type\":\"state\","
       << "\"segment_current_number\":" << data->state->segment_current_number << ","
       << "\"segments\":[";
    for (size_t i = 0; i < data->state->segments.size(); i++) {
        const Segment& seg = data->state->segments[i];
        if (i > 0) ss << ',';
        ss << "{\"target_speed_kph\":" << seg.target_speed_kph
           << ",\"distance_m\":" << static_cast<long>(seg.distance_m)
           << ",\"autoNext\":" << (seg.autoNext ? "true" : "false") << "}";
    }
    ss << "],";

    // Beep Assist and tone, so the phone renders the box's actual settings
    // rather than blank controls that only take effect once touched.
    ss << "\"beep_assist_enabled\":" << (data->state->beep_assist_enabled ? "true" : "false")
       << ",\"beep_navigation_mode\":" << (data->state->beep_navigation_mode ? "true" : "false")
       << ",\"beep_timing_mode\":" << (data->state->beep_timing_mode ? "true" : "false")
       << ",\"beep_advance_m\":" << data->state->beep_advance_m
       << ",\"beep_advance_s\":" << data->state->beep_advance_s
       << ",\"beep_waypoints_km\":\"" << formatBeepWaypointsKm(data->state->beep_waypoints_m) << "\""
       << ",\"tone_enabled\":" << (data->state->tone_enabled ? "true" : "false")
       << ",\"tone_type\":" << (data->state->simple_tone_mode ? 2 : 1);

    // Which memory slots hold a stage, so the phone can mark them the way the
    // box does instead of offering five identical buttons.
    ss << ",\"memory_populated\":[";
    for (int i = 0; i < RallyState::MAX_MEMORY_SLOTS; i++) {
        if (i > 0) ss << ',';
        ss << (data->state->memory_slots[i].empty() ? "false" : "true");
    }
    ss << "]}";
    return ss.str();
}
