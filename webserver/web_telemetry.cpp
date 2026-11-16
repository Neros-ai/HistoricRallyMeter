#include "../stage_phase.h"
#include "../sector.h"
#include "web_telemetry.h"
#include "calculations.h"
#include "callbacks.h"   // autoStartHoldActive
#include "rally_types.h"
#include "stage_file.h"
#include "counter_poller.h"
#include "rally_state.h"
#include "stage_rows.h"
#include "stage_edit.h"
#include "webserver/rally_web_server.h"
#include "ui_datetime.h"
#include "home_calc.h"
#include "surrey.h"
#include "ui_home.h"
#include "driver_trial.h"
#include "nero_frame.h"
#include "reaction_offset.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <iomanip>

NextPrevState computeNextPrevState(AppData* data) {
    NextPrevState out;
    if (!data || !data->state) return out;
    out.next_enabled = nextKeyEnabled(*data->state);
    const CounterPoll p = data->poller->getMostRecent();
    out.sector_finish = out.next_enabled && sectorFinishAvailable(*data->state);
    out.prev_enabled = prevKeyEnabled(*data->state, p.cntr1, p.cntr2);
    return out;
}

// For a figure that is genuinely KPH whatever the box is set to -- the
// segment's target speed, which is stored in KPH. calculateCurrentSpeed and
// calculateAverageSpeed already return MPH when state.units is set, so they
// must NOT come through here: doing so converted them a second time and the
// phone showed current speed and both averages 38% low beside a correct
// target. The box's own panels never had the fault; they use those functions
// directly.
// A button label for JSON. The box keys use a newline; a raw newline would
// break the object.
static std::string jsonOneLine(const char* s) {
    std::string o;
    if (!s) return o;
    for (; *s; ++s) {
        if (*s == '\n') o += "\\n";
        else if (*s == '\\' || *s == '"') { o += '\\'; o += *s; }
        else o += *s;
    }
    return o;
}


// The compact driver page the box is drawing, so the phone gauge can follow it.
// Distances and averages stay out of the dial; the phone footer draws those.
std::string driverGaugeJson(AppData* data) {
    const int page = data->tick.driverPage;
    const DriverTrialView& v = data->tick.driverTrial;
    std::string tag = "navi";
    int icon = 0;
    if (page == AppData::DP_ITALIAN_TRIAL) {
        tag = v.tag;
        icon = v.isPc ? 1 : 2;
    } else if (page == AppData::DP_GREEK_STAGE) {
        tag = "S" + std::to_string(data->tick.driverStageNo);
        icon = 1;
    }
    const bool needle = page != AppData::DP_ITALIAN_NAVI && page != AppData::DP_GREEK_IDLE;
    std::ostringstream o;
    o << "{\"page\":" << page << ",\"tag\":\"" << tag << "\",\"icon\":" << icon
      << ",\"needle\":" << (needle ? "true" : "false");
    // Every figure the driver dial last drew, word for word, with its colour band and scale.
    {
        const AppData::DriverShown& sh = data->driverShown;
        char mx[24];
        snprintf(mx, sizeof(mx), "%.3f", sh.maxSeconds);
        o << ",\"shown\":{\"speed\":\"" << jsonEscape(sh.speed) << "\",\"target\":\"" << jsonEscape(sh.target)
          << "\",\"cap\":\"" << jsonEscape(sh.targetCap) << "\",\"total_avg\":\"" << jsonEscape(sh.totalAvg)
          << "\",\"trip_avg\":\"" << jsonEscape(sh.tripAvg) << "\",\"digital\":\"" << jsonEscape(sh.digital)
          << "\",\"total_dist\":\"" << jsonEscape(sh.totalDist) << "\",\"trip_dist\":\"" << jsonEscape(sh.tripDist)
          << "\",\"zone\":" << sh.zone << ",\"max\":" << mx << "}";
    }
    if (page == AppData::DP_ITALIAN_TRIAL || page == AppData::DP_ITALIAN_NAVI) {
        // The countdown to the line, as the box prints it under the target (Home's own text).
        bool cd_ok = false;
        const double to_line = page == AppData::DP_ITALIAN_TRIAL ? v.toLineS
            : sectorSecondsToLine(*data->state, getRallyTime_ms(*data->state), &cd_ok);
        const std::string cd = (page == AppData::DP_ITALIAN_TRIAL || cd_ok) ? sectorSecondsWholeText(to_line) : std::string();
        o << ",\"target\":\"" << data->driverTrialTargetText << "\""
          << ",\"speed\":\"" << data->driverTrialSpeedText << "\""
          << ",\"cd\":\"" << cd << "\"";
    }
    if (page == AppData::DP_ITALIAN_TRIAL) {
        o << ",\"box\":\"" << countdownBoxText(v.toLineS) << "\""
          << ",\"sector_m\":" << [&] { char sb[32]; snprintf(sb, sizeof(sb), "%.3f", v.sectorTotalM); return std::string(sb); }()   // unrounded, so the phone rounds as the box does
          << ",\"avg\":\"" << (v.avgValid ? trialSpeedText(data->state->units ? kphToMph(v.avgKph) : v.avgKph) : std::string("--.--")) << "\"";
    } else if (page == AppData::DP_ITALIAN_NAVI) {
        if (data->tick.driverMinutesValid)
            o << ",\"min\":" << data->tick.driverMinutes << ",\"behind\":" << (data->tick.driverMinutesBehind ? "true" : "false");
        // The driver's Trip average as its label shows it (updateDriverDisplay: "%.2f" of
        // tick.driverTripAvg, or "--.--" in an early countdown), without reading the label back:
        // trialSpeedText is the same "%.2f", so it gives the label's text, and "--.--" was always
        // sent as 0 (atof). The tick is not recomputed between the label and this message.
        const double shown = data->tick.earlyCountdown ? 0.0 : data->tick.driverTripAvg;
        o << ",\"avg\":\"" << trialSpeedText(shown) << "\"";
    } else if (page == AppData::DP_GREEK_STAGE) {
        char sb[24];
        if (data->tick.driverSubAvgValid) {
            const double sp = data->state->units ? kphToMph(data->tick.driverSubAvgKph) : data->tick.driverSubAvgKph;
            snprintf(sb, sizeof(sb), "%.1f", sp);
        } else {
            snprintf(sb, sizeof(sb), "--.-");
        }
        o << ",\"sub\":\"" << sb << "\"";
    }
    // The needle's one chevron. Italian pc / pm: it waits, then covers the last
    // 5 s. Greek and Surrey: it walks the row. The tick is the line it arrives at.
    if (needle) {
        double prog = 0.0;
        bool tick = false;
        if (page == AppData::DP_ITALIAN_TRIAL) {
            prog = v.arrowProgress;
            tick = true;
        } else if (data->tick.inSegment) {
            prog = data->tick.segmentProgress;
            tick = true;
        }
        if (prog < 0.0) prog = 0.0;
        if (prog > 1.0) prog = 1.0;
        char cb[48];
        snprintf(cb, sizeof(cb), ",\"chev\":%.3f,\"tick\":%s", prog, tick ? "true" : "false");
        o << cb;
    }
    o << ",\"legend\":\"" << driverRightBoxLegend(page) << "\"}";
    return o.str();
}

// The Navigation stage panel exactly as the box has drawn it: the speed rows, the auto / manual word and
// time, and the arrival. The phone shows these strings and calculates nothing of its own, so the two can
// never disagree.
// The driver's T- countdown and its Early Departure caption, null when the box shows none (item 9).
static std::string autostartCdJson(AppData* data) {
    if (!(data->state->auto_start_rally_time_s > 0 && !data->tickMem.autoStartTriggered)) return "null";
    const int64_t diff_ms = autoStartRemaining_ms(data);
    if (!autoStartCountdownVisible(data->state->auto_start_early_departure, diff_ms)) return "null";
    return std::string("{\"text\":\"") + jsonEscape(autoStartCountdownText(diff_ms)) + "\",\"early\":\""
        + jsonEscape(earlyDepartureCaption(data->state->auto_start_early_departure)) + "\"}";
}

static std::string stagePanelJson(AppData* data) {
    if (!data->home) return "null";
    const HomeUi& h = *data->home;
    std::string j = "{\"rows\":[";
    bool first = true;
    for (const HomeUi::Row& r : h.rows) {
        if (r.kph.empty()) continue;
        if (!first) j += ',';
        first = false;
        j += "{\"k\":\"" + jsonEscape(r.kph) + "\",\"a\":\"" + jsonEscape(r.at) + "\",\"now\":" + (r.now ? "true" : "false") + "}";
    }
    j += "],\"auto\":" + std::string(h.autoArmed ? "true" : "false");
    j += ",\"time\":\"" + jsonEscape(h.autoTime) + "\"";
    j += ",\"arr\":" + (h.arrivalValid
        ? "{\"kph\":" + std::to_string(h.arrivalKph) + ",\"min\":" + std::to_string(h.arrivalMin)
          + ",\"text\":\"" + arrivalSpeedText(h.arrivalKph, h.unitsMph) + "\"}"
        : std::string("null"));
    j += "}";
    return j;
}

std::string buildTelemetryJson(AppData* data) {
    auto current_poll = data->poller->getMostRecent();
    // Raw count for the prop rpm (positive in reverse).
    auto raw_poll = data->poller->getMostRecentRaw();
    int64_t current_time_ms = getRallyTime_ms(*data->state);

    // The driver gauge's own figure, as the tick decided it (rally_tick.h): at rest with no stage
    // on the gauge and while an on-the-minute autostart counts down (italian: the pc / pm error).
    const double ahead_behind_s = data->tick.aheadBehindSeconds;

    NextPrevState np = computeNextPrevState(data);
    std::string rally_clock = formatTime(current_time_ms);

    // Sized for every field below plus the tone and beep objects; snprintf
    // would truncate silently and hand the phone unparseable JSON.
    // ahead_behind_s stays at 0.001 s. The digits still show tenths; rounding
    // the value here made the phone needle step on a ±1 s scale.
    char buf[2048];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"telemetry\","
        "\"rally_clock\":\"%s\","
        "\"ahead_behind_s\":%.3f,"
        "\"next_enabled\":%s,"
        "\"prev_enabled\":%s,"
        "\"sector_finish\":%s,"
        "\"units\":\"%s\","
        "\"rally_type\":%d,"
        // The ahead/behind tone the box is playing right now, so the phone
        // plays the same one (RB-WEB-02).
        "\"tone\":%s,"
        // The last Beep Assist beep, by sequence number, so the phone sounds
        // the waypoints too (RB-WEB-06).
        "\"beep\":%s}",
        rally_clock.c_str(),
        ahead_behind_s,
        np.next_enabled ? "true" : "false",
        np.prev_enabled ? "true" : "false",
        np.sector_finish ? "true" : "false",
        data->state->units ? "mph" : "kph",
        data->state->rally_type,
        toneCadenceJson(data->tick.currentTone).c_str(),
        beepEventJson(data->lastBeep).c_str());

    // Nero2.0: what the Navi, Time and Set tabs show.
    long next_m = 0;
    bool has_next_m = false;
    const RallyState& st = *data->state;
    // Italian only (the row countdown and the sector panel): one reading, one state, one position.
    const int64_t italian_stage_mm = isItalian(st) ? stagePositionMm(st, current_poll.cntr1, current_poll.cntr2) : 0;
    if (st.segment_current_number >= 0 &&
        st.segment_current_number < static_cast<long>(st.stage_segments.size())) {
        has_next_m = true;
        // Same countdown as the box: greek and surrey count to the next speed change from the stage start,
        // italian counts the row's own distance.
        if (!isItalian(st)) {
            next_m = countdownMetres(st.stage_segments, st.segment_current_number,
                                     totalShownMetres(st, current_poll.cntr1, current_poll.cntr2));
        } else {
            next_m = rowCountdownMetres(st.stage_segments, st.segment_current_number, italian_stage_mm);
        }
    }
    // Stage arrival: the tick's one figure, the same one Home draws (stage_panel.arr), in every rally type.
    const StageArrivalNow& arr = data->tick.arrival;
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
    // RB-WEB-21: italian count down as the box prints it (same sectorPanel, same text).
    std::string itCount = "null";
    std::string sectorJson = "null";
    if (isItalian(st)) {
        const long stage_m = italian_stage_mm / 1000;
        const SectorPanel sp = sectorPanel(st, current_time_ms, stage_m);
        if (sp.running) {
            itCount = "{\"km\":\"" + sectorCountdownKmText(sp, st.distance_unit) + "\",\"past\":" + (sp.metresPast > 0 ? "true" : "false") +
                      ",\"s\":" + (sp.secondsValid ? "\"" + sectorSecondsText(sp.secondsToLine) + "\"" : std::string("null")) +
                      ",\"s_past\":" + (sp.secondsValid && sp.secondsToLine < 0 ? "true" : "false") + "}";
        }
        sectorJson = sectorCardJson(st, current_time_ms, stage_m, current_poll.cntr1, current_poll.cntr2);
    }
    std::ostringstream ex;
    ex << ",\"gauge_range\":" << data->state->gauge_range_s
       << ",\"reverse\":" << (data->poller->reverse() ? "true" : "false")
       << ",\"navi_m\":" << ((isSurrey(st) || isItalian(st)) ? std::to_string(surreyNaviMetres(st, current_poll.cntr1, current_poll.cntr2)) : std::string("null"))
       << ",\"surrey\":" << (isSurrey(st) ? "true" : "false")
       << ",\"stage_panel\":" << stagePanelJson(data)
       << ",\"auto_time_past\":" << ((data->home && data->home->autoTimePast) ? "true" : "false")
       << ",\"stage_running\":" << (classifyTotalReset(st) == TotalResetCase::StageRunning ? "true" : "false")
       << ",\"stage_abort_key\":" << (goKeyAborts(st) ? "true" : "false")
       << ",\"time_taken\":\"" << (isSurrey(st) ? surreyTimeTakenText(st.time_taken_ms) : std::string()) << "\""
       << ",\"time_taken_stage\":" << (isSurrey(st) ? st.time_taken_stage : 0)
       << ",\"box_dialog\":" << openDialogJson()
       // The driver's T- countdown and Early Departure caption, as the box draws them (item 9).
       << ",\"autostart_cd\":" << autostartCdJson(data)
       // The box's warning words, sent as it shows them; empty when clear (a tap clears it).
       << ",\"counter_fault\":\"" << (data->poller && data->poller->counterFault() ? "counter fault" : "") << "\""
       << ",\"surrey_cd\":" << ((isSurrey(st) && data->home && data->home->surreyRunning)
              ? std::string("{\"m\":\"") + jsonEscape(data->home->cdMetres) + "\",\"min\":\"" + jsonEscape(data->home->cdTime) + "\"}"
              : std::string("null"))
       << ",\"it_count\":" << itCount
       << ",\"sector\":" << sectorJson
       << ",\"next_text\":\"" << (has_next_m ? formatDistance(static_cast<double>(next_m), st.distance_unit) : std::string("---")) << "\""
       // mm:ss to the next speed change, the line the box draws under row 3 (greek only).
       << ",\"speed_change\":\"" << jsonEscape(data->home ? data->home->speedLine : std::string()) << "\""
       << ",\"key_total\":\"" << jsonOneLine(readoutKeyName(st.rally_type, 0)) << "\""
       << ",\"key_trip\":\"" << jsonOneLine(readoutKeyName(st.rally_type, 1)) << "\""
       << ",\"key_navi\":\"" << jsonOneLine(readoutKeyName(st.rally_type, 2)) << "\""
       << ",\"next_key\":\"" << jsonOneLine(readoutKeyName(st.rally_type, 3)) << "\""
       << ",\"prev_key\":\"" << jsonOneLine(readoutKeyName(st.rally_type, 4)) << "\""
       << ",\"stage_no\":" << st.last_memory_slot
       << ",\"arrival\":" << (arr.valid
            ? "{\"kph\":" + std::to_string(arr.speed_kph) + ",\"min\":" + std::to_string(arr.minutes)
              + ",\"text\":\"" + arrivalSpeedText(arr.speed_kph, st.units) + "\"}"
            : std::string("null"))
       // The Time page and the start flag as the box writes them (the phone never reformats).
       << ",\"time_rally\":\"" << formatTimeTenths(current_time_ms) << "\""
       << ",\"time_computer\":\"" << formatTimeTenths(sys_ms) << "\""
       << ",\"time_offset\":\"" << formatOffsetSeconds(st.rallyTimeOffset_ms) << "\""
       << ",\"date_text\":\"" << rallyDateText(current_time_ms) << "\""
       << ",\"next_minute\":\"" << nextMinuteCaption(data) << "\""
       // The Navi zero question's two figures, as the box's dialog prints them.
       << ",\"zero_by\":\"" << [&] { char z[24]; snprintf(z, sizeof(z), "%.2f", data->tick.aheadBehindSeconds); return std::string(z); }() << "\""
       << ",\"zero_now\":\"" << [&] { char z[24]; snprintf(z, sizeof(z), "%.2f", st.ahead_behind_zero_offset_ms / 1000.0); return std::string(z); }() << "\""
       // What is typed in the box's own Enter rally time field, so the phone's field follows it (item 47).
       << ",\"dt_enter\":\"" << jsonEscape(data->timeEntry ? gtk_entry_get_text(data->timeEntry) : "") << "\""
       << ",\"phone_known\":" << (phoneClockKnown(data) ? "true" : "false")
       << ",\"tz_min\":" << tz_min
       << ",\"cal\":{\"dist_m\":" << std::fixed << std::setprecision(1)
       << countsToMeters(cavg, st.calibration) << std::defaultfloat
       // The box's own figure and unit: metres (km keeps metres), or miles to three decimals in miles.
       << ",\"dist_text\":\"" << [&] { char z[32];
              if (distIsMiles(st.distance_unit)) snprintf(z, sizeof(z), "%.3f", countsToMeters(cavg, st.calibration) / METRES_PER_MILE);
              else snprintf(z, sizeof(z), "%.1f", countsToMeters(cavg, st.calibration));
              return std::string(z); }() << "\""
       << ",\"unit\":\"" << (distIsMiles(st.distance_unit) ? "mi" : "m") << "\""
       << ",\"c1\":" << c1 << ",\"c2\":" << c2 << ",\"avg\":" << cavg
       << ",\"rpm\":" << (rpm < 0 ? -1L : std::lround(rpm)) << "}"
       << ",\"fps\":\"" << gtk_label_get_text(data->updatesPerSecLabel) << "\""
       << ",\"cpu\":\"" << gtk_label_get_text(data->cpuTempLabel) << "\""
       << ",\"distance_km\":" << (distIsKm(st.distance_unit) ? "true" : "false")
       << ",\"distance_unit\":" << st.distance_unit
       << ",\"measure_ab_bc\":" << (st.measure_ab_bc ? "true" : "false")
       // The Navi page's three figures and two elapsed times, as the box draws them.
       << ",\"home\":" << (data->home
              ? "{\"f\":[\"" + jsonEscape(data->home->figure[0]) + "\",\"" + jsonEscape(data->home->figure[1]) + "\",\""
                + jsonEscape(data->home->figure[2]) + "\"],\"el\":[\"" + jsonEscape(data->home->elapsed[0]) + "\",\""
                + jsonEscape(data->home->elapsed[1]) + "\"]}"
              : std::string("null"))
       << ",\"drv\":" << driverGaugeJson(data) << "}";
    std::string out = buf;
    out.pop_back();   // the closing brace
    return out + ex.str();
}

std::string buildStateJson(AppData* data) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(1);
    ss << "{\"type\":\"state\","
       // True when Reset Total would be asked about on the box, so the phone
       // asks the same question instead of a plain "Reset total?".
       << "\"frozen\":" << (data->state->freeze_on ? "true" : "false") << ","
       << "\"sub_reset_asks\":" << (subResetInStage(*data->state) ? "true" : "false") << ","
       << "\"total_reset_blocked\":"
       << ((sectorTotalResetBlocked(*data->state) || surreyTotalResetBlocked(*data->state)) ? "true" : "false") << ","
       << "\"total_reset_notice\":\""
       << (isSurrey(*data->state) ? SURREY_TOTAL_RESET_NOTICE : SECTOR_TOTAL_RESET_NOTICE) << "\","
       << "\"total_reset_asks\":"
       << (classifyTotalReset(*data->state) == TotalResetCase::StageRunning ? "true" : "false")
       << ",";

    // Nero2.0 Segments table, built by the same model as the box's page.
    {
        const int slot_no = stageEditSlot(data);
        const RallyState::MemorySlot& slot = data->state->memory_slots[slot_no - 1];
        std::vector<StageRow> rows = buildStageRows(data->state->segments, slot.first_number, slot.first_from_m);
        ss << "\"edit_slot\":" << slot_no << ",\"first_number\":" << slot.first_number
           << ",\"first_from_m\":" << static_cast<long>(slot.first_from_m)
           << ",\"duration_text\":\"" << (slot.sector_duration_s > 0 ? formatHMmSs(static_cast<double>(slot.sector_duration_s)) : std::string()) << "\""
           << ",\"avg_text\":\"" << jsonEscape(sectorAverageText(data->state->segments, slot.sector_duration_s, data->state->units))
           << "\",\"start_dist_text\":\"" << jsonEscape(startDistanceText(slot.start_distance_m, data->state->distance_unit))
           << "\",\"start_time\":\"";
        if (slot.start_time_s >= 0) {
            char b[16];
            snprintf(b, sizeof(b), "%02ld:%02ld:%02ld", slot.start_time_s / 3600,
                     (slot.start_time_s / 60) % 60, slot.start_time_s % 60);
            ss << b;
        }
        // As the box's stage-go key: abort only for a live stage, not a scheduled start still waiting.
        const RallyState& rs = *data->state;
        ss << "\",\"autostart_on\":" << (rs.auto_start_rally_time_s != 0 ? "true" : "false")
           << ",\"stage_locked\":" << (stageLive(rs) ? "true" : "false")
           << ",\"stage_abortable\":" << (goKeyAborts(rs) ? "true" : "false")
           << ",\"rows\":[";
        for (size_t i = 0; i < rows.size(); i++) {
            const StageRow& r = rows[i];
            if (i) ss << ',';
            // The tick boxes and the row type the phone draws; every figure is the box's text (txt).
            ss << "{\"chg\":" << (r.chg ? "true" : "false")
               << ",\"auto_av\":" << (r.auto_available ? "true" : "false")
               << ",\"auto_on\":" << (r.auto_on ? "true" : "false");
            if (i < data->state->segments.size()) {
                const Segment& sg = data->state->segments[i];
                ss << ",\"ty\":" << sg.type;
                // The box's own text for every cell: the phone shows it, never reformats.
                const SegRowText t = segRowText(sg, r, i, data->state->rally_type, data->state->distance_unit, data->state->units);
                ss << ",\"txt\":{\"num\":\"" << jsonEscape(t.num) << "\",\"ref\":\"" << jsonEscape(t.ref)
                   << "\",\"from\":\"" << jsonEscape(t.from) << "\",\"to\":\"" << jsonEscape(t.to)
                   << "\",\"dist\":\"" << jsonEscape(t.dist) << "\",\"kph\":\"" << jsonEscape(t.kph)
                   << "\",\"dur\":\"" << jsonEscape(t.dur) << "\",\"tot\":\"" << jsonEscape(t.tot) << "\"}";
            }
            ss << "}";
        }
        ss << "],";
    }
    // Trip history as the box writes it (its own four, formatDistance and its grouping).
    ss << "\"trip_history_text\":[";
    {
        std::vector<long> hist = homeTripHistory(*data->state);
        for (size_t i = 0; i < hist.size(); i++) {
            if (i) ss << ',';
            ss << '"' << jsonEscape(formatDistance(static_cast<double>(hist[i]), data->state->distance_unit)) << '"';
        }
    }
    ss << "],\"stage_logging\":" << (data->state->stage_logging ? "true" : "false")
       << ",\"sensor_logging\":" << (data->state->debug_logs_enabled ? "true" : "false")
       << ",\"force_single\":" << (data->state->force_single_display ? "true" : "false")
       << ",\"distance_km\":" << (distIsKm(data->state->distance_unit) ? "true" : "false")
       << ",\"distance_unit\":" << data->state->distance_unit
       << ",\"rally_type\":" << data->state->rally_type
       << ",\"measure_ab_bc\":" << (data->state->measure_ab_bc ? "true" : "false")
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
    ss << "\"beep_navigation_mode\":" << (data->state->beep_navigation_mode ? "true" : "false")
       << ",\"beep_timing_mode\":" << (data->state->beep_timing_mode ? "true" : "false")
       << ",\"beep_advance_m\":" << data->state->beep_advance_m
       << ",\"beep_advance_s\":" << data->state->beep_advance_s
       << ",\"tone_enabled\":" << (data->state->tone_enabled ? "true" : "false")
       << ",\"tone_type_behind\":" << data->state->tone_type_behind
       << ",\"tone_type_ahead\":" << data->state->tone_type_ahead
       << ",\"gauge_range\":" << data->state->gauge_range_s
       << ",\"reaction_text\":\"" << reactionText(data->state->reaction_offset_ms) << "\""
       << ",\"phone_beep_lead_ms\":" << data->state->phone_beep_lead_ms
       << ",\"bt_name\":\"" << jsonEscape(data->state->bluetooth_audio_name) << "\""
       << ",\"bt_known\":" << (data->state->bluetooth_audio_address.empty() ? "false" : "true")
       // The Setup page's own status line (box presses and phone presses), speaker text and address.
       << ",\"setup_status\":\"" << jsonEscape(data->setupStatusLabel ? gtk_label_get_text(data->setupStatusLabel) : data->setupStatus) << "\""
       << ",\"bt_shown\":\"" << jsonEscape(!data->state->bluetooth_audio_name.empty() ? data->state->bluetooth_audio_name
                                                                                     : data->state->bluetooth_audio_address) << "\""
       << ",\"setup_url\":\"" << jsonEscape(data->webUrlLabel ? gtk_label_get_text(data->webUrlLabel) : "") << "\""
       << ",\"version\":\"" << nero::VERSION << "\""
       << ",\"wifi\":" << (data->setupWifiJson.empty() ? "null" : data->setupWifiJson)
       << ",\"displays\":" << (data->setupDisplaysJson.empty() ? "null" : data->setupDisplaysJson);

    if (!data->uploadCheckId.empty() || !data->uploadLoaded.empty()) {
        const StageFile file = data->uploadCheckId.empty() ? StageFile() : parseStageFile(data->uploadCheckText);
        ss << ",\"upload\":" << uploadStateJson(data->uploadCheckId, *data->state, file, data->uploadLoaded);
    }

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
