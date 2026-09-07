#include "calculations.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <vector>
#include <stdexcept>

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
    if (state.segment_current_number < 0 || state.segments.empty()) {
        return 0.0;
    }
    
    double ideal_counts = 0.0;
    double remaining_time_s = elapsed_ms / 1000.0;
    
    // Go through each segment up to and including current
    for (int i = 0; i <= state.segment_current_number && i < static_cast<int>(state.segments.size()); i++) {
        const Segment& seg = state.segments[i];
        
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
    if (state.segment_current_number < 0 || state.segments.empty()) {
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
    const Segment& current_seg = state.segments[state.segment_current_number];
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

std::string segmentRowHeading(double current_speed, bool has_current_segment) {
    if (!has_current_segment) return std::string("--->");
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f >", current_speed);
    return std::string(buf);
}

std::string segmentSpeedTransition(double next_speed, bool has_next, bool is_mph) {
    char buf[32];
    if (has_next)
        snprintf(buf, sizeof(buf), "> %.0f %s", next_speed, is_mph ? "mph" : "kph");
    else
        snprintf(buf, sizeof(buf), "> END");
    return std::string(buf);
}

std::vector<double> parseBeepWaypointsKm(const std::string& text) {
    std::vector<double> waypoints;

    // Commas and semicolons both separate: the numeric keypad offers ";",
    // but a list pasted from a roadbook is usually comma-separated.
    std::string normalised = text;
    for (char& c : normalised)
        if (c == ',' || c == ';' || c == '\n' || c == '\t') c = ' ';

    std::istringstream stream(normalised);
    std::string token;
    while (stream >> token) {
        try {
            size_t consumed = 0;
            double km = std::stod(token, &consumed);
            // Reject trailing junk ("3.67m") and negative distances, but keep
            // parsing: one bad token must not cost the operator the rest.
            if (consumed != token.size() || km < 0.0) continue;
            waypoints.push_back(km * 1000.0);
        } catch (const std::exception&) {
            continue;
        }
    }

    // Travel order, and no duplicates -- a repeated waypoint would beep twice
    // at the same point.
    std::sort(waypoints.begin(), waypoints.end());
    waypoints.erase(std::unique(waypoints.begin(), waypoints.end(),
                                [](double a, double b) { return std::abs(a - b) < 0.5; }),
                    waypoints.end());
    return waypoints;
}

std::string formatBeepWaypointsKm(const std::vector<double>& waypoints_m) {
    std::string out;
    char buf[32];
    for (size_t i = 0; i < waypoints_m.size(); i++) {
        snprintf(buf, sizeof(buf), "%.2f", waypoints_m[i] / 1000.0);
        if (i > 0) out += ", ";
        out += buf;
    }
    return out;
}

size_t beepCursorFor(const std::vector<double>& waypoints_m, double travelled_m) {
    size_t i = 0;
    while (i < waypoints_m.size() && waypoints_m[i] <= travelled_m) i++;
    return i;
}

double idealSecondsToReachDistance(const std::vector<Segment>& segments, double target_distance_m) {
    double cumulative_m = 0.0;
    double cumulative_s = 0.0;
    for (const auto& seg : segments) {
        // A zero-LENGTH segment contributes nothing either way, so skip it.
        if (seg.distance_m <= 0.0) continue;
        // A segment with real distance but no target speed is an incomplete
        // roadbook, not an empty one. Skipping it would drop its DISTANCE as
        // well as its time, shortening the roadbook and firing every later
        // timing beep early by the missing segment's duration. Everything at
        // or beyond it is genuinely undefined.
        if (seg.target_speed_kph <= 0.0) return -1.0;
        double speed_m_per_s = seg.target_speed_kph * (1000.0 / 3600.0);
        double segment_time_s = seg.distance_m / speed_m_per_s;
        if (cumulative_m + seg.distance_m >= target_distance_m) {
            double remaining_m = target_distance_m - cumulative_m;
            return cumulative_s + (remaining_m / speed_m_per_s);
        }
        cumulative_m += seg.distance_m;
        cumulative_s += segment_time_s;
    }
    return -1.0;  // the waypoint lies beyond what the loaded segments cover
}

size_t beepTimingCursorFor(const std::vector<double>& waypoints_m, double elapsed_stage_s,
                           const std::vector<Segment>& segments, double advance_s) {
    size_t i = 0;
    while (i < waypoints_m.size()
           && timingBeepDue(waypoints_m[i], elapsed_stage_s, segments, advance_s)) i++;
    return i;
}

bool navigationBeepDue(double waypoint_m, double travelled_m, double advance_m) {
    return travelled_m >= waypoint_m - advance_m;
}

bool timingBeepDue(double waypoint_m, double elapsed_stage_s,
                   const std::vector<Segment>& segments, double advance_s) {
    double ideal_s = idealSecondsToReachDistance(segments, waypoint_m);
    if (ideal_s < 0.0) return false;
    return elapsed_stage_s >= ideal_s - advance_s;
}

long dueBeepWaypoint(const std::vector<double>& waypoints_m, size_t from_index,
                     double travelled_m,
                     bool navigation_mode, double advance_m,
                     bool timing_mode, double advance_s,
                     bool stage_active, double elapsed_stage_s,
                     const std::vector<Segment>& segments) {
    // Return the EARLIEST waypoint that is due, not the newest: a long
    // polling gap that passes several at once must report them in order
    // rather than silently swallowing the ones behind.
    for (size_t i = from_index; i < waypoints_m.size(); i++) {
        bool nav_due = navigation_mode && navigationBeepDue(waypoints_m[i], travelled_m, advance_m);
        bool timing_due = timing_mode && stage_active
                        && timingBeepDue(waypoints_m[i], elapsed_stage_s, segments, advance_s);
        if (nav_due || timing_due) return static_cast<long>(i);
    }
    return -1;
}

double pulsesPerKm(long calibration) {
    if (calibration <= 0) return 0.0;
    // kph = counts_per_hour * calibration / 1e9, so one km takes
    // 1e9 / calibration counts.
    return 1e9 / static_cast<double>(calibration);
}

std::string calibrationReadoutLine(long distance_m, int64_t counts_avg,
                                   int64_t counts_s1, int64_t counts_s2) {
    std::stringstream ss;
    ss << "Device distance: " << distance_m << "m."
       << " Pulses " << counts_avg
       << " S1=" << counts_s1 << " S2=" << counts_s2;
    return ss.str();
}

long calibrationFromPulsesPerKm(double pulses_per_km) {
    if (pulses_per_km <= 0.0) return 0;
    return static_cast<long>((1e9 / pulses_per_km) + 0.5);
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

CompactGaugeLayout computeCompactGaugeLayout(double width, double height) {
    constexpr double REF_RADIUS = 256.0;  // gauge radius in the 1280x400 layout

    CompactGaugeLayout L{};
    // The gauge fills the panel width (bezel ~18px + a small margin); the hub
    // sits low, leaving room for the readout box and the footer beneath it.
    L.radius  = std::min(width / 2 - 25, height - 95);
    L.centerX = width / 2;
    L.centerY = height - 75;
    // Clamped at 1.0: a larger gauge must not scale the fonts UP, or the
    // values run off the panel.
    L.fscale  = std::min(1.0, L.radius / REF_RADIUS);

    L.valSize    = 44 * L.fscale;
    L.curTopSize = 50 * L.fscale;  // RB-DRV-08: Current/Target/Total/Trip's enlarged size
    L.labelSize  = 16 * L.fscale;
    L.labelGap   = 8 * L.fscale;
    L.rowGap     = 48 * L.fscale;

    // rightAnchor: symmetric about the hub, far enough out to clear the arc.
    // Every Total/Trip speed value right-aligns to it.
    L.rightAnchor = L.centerX + L.radius * 0.72;
    // distanceAnchor: where every distance value AND the "Distance (metres)"
    // caption below it (RB-DRV-02) both right-align to, so a value's last
    // digit and the caption's closing ")" always share one vertical edge --
    // no text measurement needed, both anchor to the identical X. RB-DRV-08
    // shifts this 10px right of the arc-derived offset.
    L.distanceAnchor = L.radius * 0.72 + 10.0;

    // Coloured band arc is drawn at `radius` with lineWidth 12, so its outer
    // edge sits radius+6 out from the hub. Current's value right-aligns to,
    // and top-aligns with, this arc's bottom-right end / topmost point
    // (RB-DRV-08); Target mirrors it on the left.
    L.bandOuterX  = L.centerX + (L.radius + 6.0);
    L.bandTargetX = L.centerX - (L.radius + 6.0);
    L.bandTopY    = L.centerY - (L.radius + 6.0);

    // The ahead/behind readout is lifted so its top edge sits above the hub.
    // Drawn after the needle, it paints the hub out -- the hub carries no
    // information the driver needs, and the readout is what they actually
    // look at, so it gets the centre of the dial.
    L.boxHeight = 50.0;
    L.boxY      = L.centerY - 10;

    // Total/Trip rows: RB-DRV-08 draws them at curTopSize instead of valSize
    // and nudges them up, with extra line spacing, so the taller text's
    // descenders clear the ahead/behind box sitting above them.
    const double lineGap = L.rowGap + 10 * L.fscale;
    L.tripBaseline  = L.boxY - (0.22 * L.curTopSize + 6 * L.fscale);
    L.totalBaseline = L.tripBaseline - lineGap;

    // Footer rides on the bottom edge of the box, so the two never overlap
    // however the box is sized.
    L.footSize     = std::max(11.0, 14 * L.fscale);
    L.footBaseline = L.boxY + L.boxHeight;

    // Captions were previously hung off the box's top edge. The box has
    // moved up over the hub, so they get their own offset below the value
    // rows, independent of wherever the box now sits.
    L.captionBaseline = L.centerY + 20 + L.labelSize * 0.78;

    return L;
}

std::string distanceColumnCaption(const char* unit) {
    const bool kilometres = (unit != nullptr) && (std::string(unit) == "km");
    return std::string("Distance (") + (kilometres ? "kilometres" : "metres") + ")";
}

std::string speedColumnCaption(bool units_mph) {
    return units_mph ? "Average Speed (Mph)" : "Average Speed (Kmh)";
}

NeedleGeometry computeNeedleGeometry(double seconds, double max_seconds, double radius) {
    NeedleGeometry n{};

    // Peg at the scale ends. Past full deflection the needle stops moving and
    // the digital readout carries the real magnitude -- a needle that wrapped
    // round would read as the opposite error.
    double clamped = seconds;
    if (clamped > max_seconds)  clamped = max_seconds;
    if (clamped < -max_seconds) clamped = -max_seconds;

    n.angle = M_PI + M_PI / 2 + (clamped / max_seconds) * (M_PI / 2);
    // 5% short of the tick ring so the bar's flat tip does not foul the ticks
    // it is being read against.
    n.length = (radius - 10) * 0.95;
    n.halfWidth = 3.0;
    return n;
}

double gaugeEffectiveMaxSeconds(double seconds) {
    double abs_sec = std::abs(seconds);
    if (abs_sec < 3.0) return 3.0;
    if (abs_sec > 30.0) return 30.0;
    return abs_sec;
}

int gaugeZone(double seconds) {
    double abs_sec = std::abs(seconds);
    if (abs_sec < 10.0) return 0;
    if (abs_sec < 30.0) return 1;
    return 2;
}

GaugeArcColor gaugeArcColor(int zone) {
    switch (zone) {
    case 0:  return { 0.0, 0.7, 0.0 };    // green
    case 2:  return { 0.8, 0.1, 0.1 };    // red
    default: return { 0.85, 0.65, 0.0 };  // amber
    }
}

std::string gaugeTickLabel(int index) {
    if (index == 0) return std::string();
    return std::to_string(index);
}

bool gaugeTickLabelsVisible(double seconds) {
    return gaugeZone(seconds) == 0;
}

double gaugeTickAngle(int index, double max_val) {
    double frac = static_cast<double>(index) / max_val;
    return M_PI + M_PI / 2 + frac * (M_PI / 2);
}

std::vector<double> parseSemicolonList(const std::string& input) {
    std::vector<double> result;
    std::stringstream stream(input);
    std::string token;
    while (std::getline(stream, token, ';')) {
        if (token.empty()) continue;
        result.push_back(std::stod(token));
    }
    return result;
}

bool buildSegmentSpeedDistancePairs(const std::vector<double>& speeds,
                                     const std::vector<double>& distances,
                                     std::vector<std::pair<double, double>>& out) {
    out.clear();
    if (speeds.empty() || distances.empty()) return false;

    if (speeds.size() == 1) {
        for (double d : distances) {
            out.emplace_back(speeds[0], d);
        }
        return true;
    }

    if (speeds.size() != distances.size()) {
        return false;
    }

    for (size_t i = 0; i < speeds.size(); i++) {
        out.emplace_back(speeds[i], distances[i]);
    }
    return true;
}
