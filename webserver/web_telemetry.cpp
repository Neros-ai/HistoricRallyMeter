#include "web_telemetry.h"
#include "calculations.h"
#include "callbacks.h"   // autoStartHoldActive
#include "rally_types.h"
#include "counter_poller.h"
#include "rally_state.h"
#include "stage_rows.h"
#include "stage_edit.h"
#include "webserver/rally_web_server.h"
#include "ui_datetime.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <iomanip>

NextPrevState computeNextPrevState(AppData* data) {
    NextPrevState out;
    if (!data || !data->state) return out;
    out.next_enabled = nextAvailable(*data->state);
    out.prev_enabled = prevAvailable(*data->state);
    return out;
}

// For a figure that is genuinely KPH whatever the box is set to -- the
// segment's target speed, which is stored in KPH. calculateCurrentSpeed and
// calculateAverageSpeed already return MPH when state.units is set, so they
// must NOT come through here: doing so converted them a second time and the
// phone showed current speed and both averages 38% low beside a correct
// target. The box's own panels never had the fault; they use those functions
// directly.
static double displayKph(AppData* data, double kph) {
    if (data->state->units) return kph * 0.621371;
    return kph;
}

std::string buildTelemetryJson(AppData* data) {
    auto current_poll = data->poller->getMostRecent();
    auto tenth_poll = data->poller->get10th();
    // Speed is measured on the raw count so it stays positive in reverse.
    auto raw_poll = data->poller->getMostRecentRaw();
    int64_t current_time_ms = getRallyTime_ms(*data->state);

    int64_t trip_count_diff = calculateDistanceCounts(*data->state,
        current_poll.cntr1, current_poll.cntr2,
        data->state->trip_start_cntr1, data->state->trip_start_cntr2);
    long trip_m = adjustedDistanceMeters(
        countsToCentimeters(trip_count_diff, data->state->calibration),
        data->state->trip_distance_adjust_cm);
    const bool hold_at_zero = autoStartHoldActive(data);
    double trip_avg = averageSpeedForDisplay(calculateAverageSpeed(*data->state,
        data->state->trip_start_time_ms, current_time_ms, trip_count_diff,
        data->state->trip_distance_adjust_cm), hold_at_zero);

    int64_t total_count_diff = calculateDistanceCounts(*data->state,
        current_poll.cntr1, current_poll.cntr2,
        data->state->total_start_cntr1, data->state->total_start_cntr2);
    long total_m = adjustedDistanceMeters(
        countsToCentimeters(total_count_diff, data->state->calibration),
        data->state->total_distance_adjust_cm);
    double total_avg = averageSpeedForDisplay(calculateAverageSpeed(*data->state,
        data->state->total_start_time_ms, current_time_ms, total_count_diff,
        data->state->total_distance_adjust_cm), hold_at_zero);

    double cur_speed = calculateCurrentSpeed(*data->state, raw_poll, tenth_poll);
    if (cur_speed < 0) cur_speed = 0.0;

    double target_kph = 0.0;
    double ahead_behind_s = 0.0;
    if (data->state->segment_current_number >= 0 &&
        data->state->segment_current_number < static_cast<long>(data->state->stage_segments.size())) {
        const Segment& seg = data->state->stage_segments[data->state->segment_current_number];
        target_kph = countsPerHourToKPH(seg.target_speed_counts_per_hour, data->state->calibration);
        // The corrected stage distance, matching ui_driver: see
        // stageCountsCorrected. total_count_diff above stays raw because
        // adjustedDistanceMeters applies the same correction to the odometer
        // itself, and applying it twice would double it.
        ahead_behind_s = calculateAheadBehindFromStageStart(*data->state, current_time_ms,
            stageCountsCorrected(data, current_poll));
        ahead_behind_s += data->state->ahead_behind_zero_offset_ms / 1000.0;
        // The same hold the driver display applies (ui_driver.cpp). Held here
        // too, or the phone would show the previous stage's error climbing at
        // the line while the driver's gauge sits at rest -- two screens
        // disagreeing about whether the crew are on time.
        if (hold_at_zero) ahead_behind_s = 0.0;
    }

    NextPrevState np = computeNextPrevState(data);
    std::string rally_clock = formatTime(current_time_ms);

    // Sized for every field below plus the tone and beep objects; snprintf
    // would truncate silently and hand the phone unparseable JSON.
    char buf[2048];
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
        "\"next_enabled\":%s,"
        "\"prev_enabled\":%s,"
        "\"units\":\"%s\","
        // The ahead/behind tone the box is playing right now, so the phone
        // plays the same one (RB-WEB-02).
        "\"tone\":%s,"
        // The last Beep Assist beep, by sequence number, so the phone sounds
        // the waypoints too (RB-WEB-06).
        "\"beep\":%s}",
        rally_clock.c_str(),
        trip_m,
        total_m,
        cur_speed,      // already in the box's units
        trip_avg,       // already in the box's units
        total_avg,      // already in the box's units
        displayKph(data, target_kph),   // stored in KPH; convert
        ahead_behind_s,
        data->state->segment_current_number >= 0 ? data->state->segment_current_number + 1 : 0,
        data->state->stage_segments.size(),
        np.next_enabled ? "true" : "false",
        np.prev_enabled ? "true" : "false",
        data->state->units ? "mph" : "kph",
        toneCadenceJson(data->currentTone).c_str(),
        beepEventJson(data->lastBeep).c_str());

    // Nero2.0: what the Navi, Time and Set tabs show.
    int64_t total_s = elapsedForDisplay((current_time_ms - data->state->total_start_time_ms) / 1000, hold_at_zero);
    int64_t trip_s = elapsedForDisplay((current_time_ms - data->state->trip_start_time_ms) / 1000, hold_at_zero);
    std::string speed_change;
    long next_m = 0;
    bool has_next_m = false;
    const RallyState& st = *data->state;
    if (st.segment_current_number >= 0 &&
        st.segment_current_number < static_cast<long>(st.stage_segments.size())) {
        const Segment& cur = st.stage_segments[st.segment_current_number];
        int64_t seg_diff = segmentCountsCorrected(data, current_poll);
        long rem_cm = countsToCentimeters(static_cast<int64_t>(cur.distance_counts) - seg_diff, st.calibration);
        next_m = (rem_cm >= 0) ? (rem_cm + 99) / 100 : -((-rem_cm) / 100);
        has_next_m = true;
        if (st.segment_current_number + 1 < static_cast<long>(st.stage_segments.size())) {
            speed_change = speedChangeLine(cur.target_speed_kph,
                st.stage_segments[st.segment_current_number + 1].target_speed_kph, true, st.units);
        }
    }
    // Stage arrival
    long start_s = -1, start_dist = 0;
    if (st.last_memory_slot >= 1 && st.last_memory_slot <= RallyState::MAX_MEMORY_SLOTS) {
        start_s = st.memory_slots[st.last_memory_slot - 1].start_time_s;
        start_dist = st.memory_slots[st.last_memory_slot - 1].start_distance_m;
    }
    StageArrival arr;
    if (start_s >= 0) {
        time_t t = current_time_ms / 1000;
        struct tm* lt = localtime(&t);
        if (lt) {
            long tod = lt->tm_hour * 3600L + lt->tm_min * 60L + lt->tm_sec;
            arr = stageArrival(static_cast<double>(start_dist), (start_s - tod) / 60.0);
        }
    }
    const int64_t sys_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    // The box's zone, so the phone shows the box's clock time whatever its own zone.
    long tz_min = 0;
    {
        time_t t = sys_ms / 1000;
        struct tm lt;
        if (localtime_r(&t, &lt)) tz_min = lt.tm_gmtoff / 60;
    }
    // Calibration page figures: counts since "zero counters" (or the Total
    // start), and the prop/wheel rpm.
    const uint64_t cs1 = data->cal_started ? data->cal_start_cntr1 : st.total_start_cntr1;
    const uint64_t cs2 = data->cal_started ? data->cal_start_cntr2 : st.total_start_cntr2;
    const int64_t c1 = static_cast<int64_t>(current_poll.cntr1) - static_cast<int64_t>(cs1);
    const int64_t c2 = static_cast<int64_t>(current_poll.cntr2) - static_cast<int64_t>(cs2);
    const int64_t cavg = st.counters ? (c1 + c2) / 2 : c1;
    CounterPoll tenth = data->poller->get10th();
    const int64_t rpm_ms = tenth.time_ms ? raw_poll.time_ms - tenth.time_ms : 0;
    const double rpm = propRpmFromCounts(raw_poll.cntr1, tenth.cntr1, rpm_ms, st.prop_pulses_per_rev);
    std::ostringstream ex;
    ex << ",\"reverse\":" << (data->poller->reverse() ? "true" : "false")
       << ",\"total_s\":" << total_s << ",\"trip_s\":" << trip_s
       << ",\"next_m\":" << (has_next_m ? std::to_string(next_m) : std::string("null"))
       << ",\"speed_change\":\"" << speed_change << "\""
       << ",\"stage_no\":" << st.last_memory_slot
       << ",\"arrival\":" << (arr.valid
            ? "{\"kph\":" + std::to_string(arr.speed_kph) + ",\"min\":" + std::to_string(arr.minutes) + "}"
            : std::string("null"))
       << ",\"computer_ms\":" << sys_ms
       << ",\"rally_offset_ms\":" << st.rallyTimeOffset_ms
       << ",\"phone_known\":" << (phoneClockKnown(data) ? "true" : "false")
       << ",\"tz_min\":" << tz_min
       << ",\"cal\":{\"dist_m\":" << std::fixed << std::setprecision(1)
       << countsToMeters(cavg, st.calibration) << std::defaultfloat
       << ",\"c1\":" << c1 << ",\"c2\":" << c2 << ",\"avg\":" << cavg
       << ",\"rpm\":" << (rpm < 0 ? -1L : std::lround(rpm)) << "}"
       << ",\"fps\":\"" << gtk_label_get_text(data->updatesPerSecLabel) << "\""
       << ",\"cpu\":\"" << gtk_label_get_text(data->cpuTempLabel) << "\"}";
    std::string out = buf;
    out.pop_back();   // the closing brace
    return out + ex.str();
}

std::string buildStateJson(AppData* data) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "{\"type\":\"state\","
       << "\"segment_current_number\":" << data->state->segment_current_number << ","
       // True when Reset Total would be asked about on the box, so the phone
       // asks the same question instead of a plain "Reset total?".
       << "\"frozen\":" << (data->homeFrozen ? "true" : "false") << ","
       << "\"total_reset_asks\":"
       << (classifyTotalReset(*data->state) == TotalResetCase::StageRunning ? "true" : "false")
       << ","
       << "\"segments\":[";
    for (size_t i = 0; i < data->state->segments.size(); i++) {
        const Segment& seg = data->state->segments[i];
        if (i > 0) ss << ',';
        ss << "{\"target_speed_kph\":" << seg.target_speed_kph
           << ",\"distance_m\":" << static_cast<long>(seg.distance_m)
           << ",\"autoNext\":" << (seg.autoNext ? "true" : "false") << "}";
    }
    ss << "],";

    // Nero2.0 Segments table, built by the same model as the box's page.
    {
        const int slot_no = stageEditSlot(data);
        const RallyState::MemorySlot& slot = data->state->memory_slots[slot_no - 1];
        std::vector<StageRow> rows = buildStageRows(data->state->segments, slot.first_number, slot.first_from_m);
        ss << "\"edit_slot\":" << slot_no << ",\"first_number\":" << slot.first_number
           << ",\"first_from_m\":" << static_cast<long>(slot.first_from_m)
           << ",\"start_distance_m\":" << slot.start_distance_m
           << ",\"start_time\":\"";
        if (slot.start_time_s >= 0) {
            char b[16];
            snprintf(b, sizeof(b), "%02ld:%02ld:%02ld", slot.start_time_s / 3600,
                     (slot.start_time_s / 60) % 60, slot.start_time_s % 60);
            ss << b;
        }
        // As the box's stage-go key: abort only for a live stage, not a scheduled start still waiting.
        const RallyState& rs = *data->state;
        const bool sched_pending = rs.auto_start_rally_time_s != 0 && !rs.auto_start_early_departure &&
                                   rs.segment_current_number < 0;
        ss << "\",\"autostart_on\":" << (rs.auto_start_rally_time_s != 0 ? "true" : "false")
           << ",\"stage_locked\":" << (stageLive(rs) ? "true" : "false")
           << ",\"stage_abortable\":" << (stageAbortable(rs) && !sched_pending ? "true" : "false")
           << ",\"rows\":[";
        for (size_t i = 0; i < rows.size(); i++) {
            const StageRow& r = rows[i];
            if (i) ss << ',';
            ss << "{\"n\":" << r.number << ",\"from\":" << static_cast<long>(r.from_m)
               << ",\"to\":" << static_cast<long>(r.to_m) << ",\"dist\":" << static_cast<long>(r.dist_m)
               << ",\"chg\":" << (r.chg ? "true" : "false") << ",\"kph\":" << r.kph
               << ",\"ms\":" << r.ms << ",\"auto_av\":" << (r.auto_available ? "true" : "false")
               << ",\"auto_on\":" << (r.auto_on ? "true" : "false")
               << ",\"time_s\":" << std::lround(r.time_s) << ",\"total_s\":" << std::lround(r.total_s) << "}";
        }
        ss << "],";
    }
    ss << "\"trip_history\":[";
    {
        std::vector<long> hist = parseTripHistory(data->state->trip_history_m);
        for (size_t i = 0; i < hist.size(); i++) { if (i) ss << ','; ss << hist[i]; }
    }
    ss << "],\"stage_logging\":" << (data->state->stage_logging ? "true" : "false")
       << ",\"sensor_logging\":" << (data->state->debug_logs_enabled ? "true" : "false")
       << ",\"force_single\":" << (data->state->force_single_display ? "true" : "false")
       << ",\"calibration_pulses\":" << displayPulsesPerKm(data->state->typed_pulses_per_km, data->state->calibration)
       << ",\"sensors_both\":" << (data->state->counters ? "true" : "false")
       << ",\"prop_pulses\":" << data->state->prop_pulses_per_rev
       << ",\"web_url\":\"" << (data->webServer ? data->webServer->getWebUrl() : std::string()) << "\",";

    // The autostart line the co-pilot's stage panel prints, so the phone
    // shows the same words rather than re-deriving them from a target time
    // it would have to be told the epoch for.
    ss << "\"autostart\":\""
       << formatAutoStartStatus(data->state->auto_start_rally_time_s,
                                data->state->auto_start_early_departure,
                                getAutoStartEpochMs())
       << "\",";

    // Beep Assist and tone, so the phone renders the box's actual settings
    // rather than blank controls that only take effect once touched.
    ss << "\"beep_assist_enabled\":" << (data->state->beep_assist_enabled ? "true" : "false")
       << ",\"beep_navigation_mode\":" << (data->state->beep_navigation_mode ? "true" : "false")
       << ",\"beep_timing_mode\":" << (data->state->beep_timing_mode ? "true" : "false")
       << ",\"beep_advance_m\":" << data->state->beep_advance_m
       << ",\"beep_advance_s\":" << data->state->beep_advance_s
       << ",\"tone_enabled\":" << (data->state->tone_enabled ? "true" : "false")
       << ",\"tone_type\":" << data->state->tone_type_behind
       << ",\"tone_type_behind\":" << data->state->tone_type_behind
       << ",\"tone_type_ahead\":" << data->state->tone_type_ahead;

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
