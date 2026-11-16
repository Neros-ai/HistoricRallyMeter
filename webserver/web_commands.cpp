#include <cmath>
#include "web_commands.h"
#include "../ui_copilot.h"
#include "config_file.h"
#include "calculations.h"
#include "callbacks.h"
#include "stage_phase.h"
#include "sector.h"
#include "surrey.h"
#include "rally_types.h"
#include "stage_store.h"
#include "rally_state.h"
#include "stage_edit.h"
#include "stage_rows.h"
#include "ui_home.h"
#include "ui_datetime.h"
#include "ui_calibration.h"
#include "ui_settings.h"
#include "stage_file.h"
#include "reaction_offset.h"
#include "sector.h"
#include "counter_poller.h"
#include "sector_edit.h"
#include "ui_setup.h"
#include "ui_segments.h"
#include <chrono>

#include <cctype>
#include <cstring>
#include <string>

// Where the value of "key" starts in the frame, just past `"key":` (and past the opening quote of a string
// value); nullptr when the key is absent. The typed finders below all begin here.
static const char* jsonFindKey(const char* json, const char* key, bool string_value) {
    std::string needle = std::string("\"") + key + (string_value ? "\":\"" : "\":");
    const char* p = strstr(json, needle.c_str());
    return p ? p + needle.size() : nullptr;
}

static const char* jsonFindString(const char* json, const char* key, char* out, size_t out_len) {
    const char* p = jsonFindKey(json, key, true);
    if (!p) return nullptr;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < out_len) out[i++] = *p++;
    out[i] = '\0';
    return out;
}

// Whole-value form, with no fixed output buffer. The waypoint list can
// outgrow any fixed buffer, and a truncated list is worse than none: the cut
// lands mid-number, so the fragment parses as a real waypoint the operator
// never entered and every waypoint after it is silently dropped.
//
// Which is exactly why the value must be closed by a quote before it is
// accepted. A frame cut short in transit ends with no closing quote, and
// returning what was read so far would commit that fragment -- the very
// truncation this form exists to avoid. The length cap is a second guard:
// a value longer than any plausible waypoint list is a malformed or hostile
// frame, not a roadbook.
static const size_t JSON_MAX_STRING_VALUE = 64 * 1024;

static bool jsonFindStringValue(const char* json, const char* key, std::string* out) {
    const char* p = jsonFindKey(json, key, true);
    if (!p) return false;
    std::string value;
    while (*p && *p != '"') {
        if (value.size() >= JSON_MAX_STRING_VALUE) return false;
        value.push_back(*p++);
    }
    // Ran off the end of the buffer rather than reaching the closing quote.
    if (*p != '"') return false;
    *out = value;
    return true;
}

static bool jsonFindBool(const char* json, const char* key, bool* out) {
    const char* p = jsonFindKey(json, key, false);
    if (!p) return false;
    while (*p && isspace(static_cast<unsigned char>(*p))) p++;
    if (strncmp(p, "true", 4) == 0) { *out = true; return true; }
    if (strncmp(p, "false", 5) == 0) { *out = false; return true; }
    return false;
}

static bool jsonFindInt(const char* json, const char* key, int* out) {
    const char* p = jsonFindKey(json, key, false);
    if (!p) return false;
    *out = static_cast<int>(strtol(p, nullptr, 10));
    return true;
}

static bool jsonFindDouble(const char* json, const char* key, double* out) {
    const char* p = jsonFindKey(json, key, false);
    if (!p) return false;
    *out = strtod(p, nullptr);
    return true;
}

// A command handler: the frame, and the command's own name (for the handlers that serve several).
// The return value is webHandleCommand's: true when rally state changed.
typedef bool (*CommandHandler)(AppData* data, const char* json, const char* type);

// ---- Box keys -----------------------------------------------------------------------------------------------

static bool cmdPhoneLeadStep(AppData* data, const char* json, const char*) {
    // Phone sound lead: 10 ms a press, 0..400, the same setting as the box's Settings page.
    int ms = 0;
    if (!jsonFindInt(json, "ms", &ms) || (ms != 10 && ms != -10)) return false;
    data->state->phone_beep_lead_ms = clampPhoneLeadMs(data->state->phone_beep_lead_ms + ms);
    updateSettingsDisplay(data);
    saveConfigSoon(data);   // a held +/- key is one write
    return true;
}

static bool cmdResetTrip(AppData* data, const char* json, const char*) {
    // In a pc / pm the phone asks first and sends choice "confirm".
    char choice[16] = "";
    jsonFindString(json, "choice", choice, sizeof(choice));
    if (subResetInStage(*data->state) && strcmp(choice, "confirm") != 0) return false;
    applyTripReset(data);
    return true;
}

static bool cmdResetTotal(AppData* data, const char* json, const char*) {
    // With a stage running the phone sends "reset_total_capture" on the
    // press, asks the box's question, and sends choice "confirm" only if
    // the crew confirm. If the stage was driven out meanwhile, the confirm
    // is the plain idle reset, as on the box.
    if (sectorTotalResetBlocked(*data->state)) return false;   // italian, sector running: as the box
    char choice[16] = "";
    jsonFindString(json, "choice", choice, sizeof(choice));
    return applyTotalReset(data, strcmp(choice, "confirm") == 0
                                     ? TotalResetChoice::ConfirmDistanceReset
                                     : TotalResetChoice::Cancel);
}

static bool cmdResetTotalCapture(AppData* data, const char*, const char*) {
    // The press itself: the distance zero, if the crew then confirm.
    if (!sectorTotalResetBlocked(*data->state) && !surreyTotalResetBlocked(*data->state) &&
        classifyTotalReset(*data->state) == TotalResetCase::StageRunning) {
        captureTotalResetPress(data);
    }
    return false;
}

static bool cmdResetTotalCancel(AppData* data, const char*, const char*) {
    // The crew answered Back on the phone. Drops the captured press and
    // does nothing else -- see cancelTotalResetPress for why this is its
    // own message rather than a reset_total.
    cancelTotalResetPress(data);
    return false;
}

static bool cmdDistanceAdjust(AppData* data, const char* json, const char*) {
    // Same arithmetic as the co-pilot's -10/+10 buttons, via the shared
    // helper. The step comes from the phone so the two ends can differ
    // later without a second command.
    int delta_m = 0;
    if (!jsonFindInt(json, "delta_m", &delta_m)) return false;
    // A "correction" of hundreds of metres is a stuck key or a bad frame,
    // not a wheel-slip fix; Reset Total is the tool for that.
    if (delta_m == 0 || delta_m < -1000 || delta_m > 1000) return false;
    applyDistanceAdjust(data, delta_m);
    return true;
}

static bool cmdDistanceSet(AppData* data, const char* json, const char*) {
    double meters = 0.0;
    if (!jsonFindDouble(json, "meters", &meters)) return false;
    // Six digits, matching the entry cap on the box's dialog: past
    // 999,999 m the display switches to km and reads as truncated.
    if (meters < 0.0 || meters > 999999.0) return false;
    return applyDistanceSet(data, meters);
}

// The navi key (Italian only). Greek's third key is the countdown label and surrey's is "stage",
// a figure with nothing to set.
static bool cmdNaviSet(AppData* data, const char* json, const char*) {
    if (!data->state || !isItalian(*data->state) || !data->poller) return false;
    bool reset = false;
    jsonFindBool(json, "reset", &reset);
    double navi_m = 0.0;
    if (!reset && (!jsonFindDouble(json, "meters", &navi_m) || navi_m < 0.0 || navi_m > 999999.0)) return false;
    return applyNaviSet(data, reset, navi_m);
}

static bool cmdNextPrev(AppData* data, const char* json, const char* type) {
    if (strcmp(type, "next") == 0) {
        // Ending an italian sector is asked first; the phone sends confirm on yes.
        bool confirmed = false;
        jsonFindBool(json, "confirm", &confirmed);
        if (sectorFinishAvailable(*data->state) && !confirmed) return false;
        on_next_press(nullptr, data);
    }
    else on_prev_press(nullptr, data);
    return true;
}

// ---- the stage on show (Segments) -- the same calls the box's page makes ----

static bool cmdEditSelect(AppData* data, const char* json, const char*) {
    int slot = 0;
    if (!jsonFindInt(json, "slot", &slot) || slot < 1 || slot > RallyState::MAX_MEMORY_SLOTS) return false;
    stageEditSelect(data, slot);
    refreshSegmentList(data);
    return true;
}

static bool cmdRowsAdd(AppData* data, const char* json, const char*) {
    std::string text;
    if (!jsonFindStringValue(json, "text", &text)) return false;
    if (stageEditLocked(*data->state)) { data->webNotice = STAGE_EDIT_LOCKED_NOTICE; return false; }
    std::string rest, notice;
    int added = stageEditAddDistances(data, text, &rest, &notice);
    refreshSegmentList(data);
    // The box's words for what was not taken, as its add distance key flashes them.
    if (!rest.empty())
        data->webNotice = !notice.empty() ? notice + ": " + rest : "Not a distance (it must be more than 0): " + rest;
    else if (!notice.empty())
        data->webNotice = notice;
    return added > 0 && rest.empty();
}

// The roadbook box number typed on any greek row, as the box's "box" entries.
static bool cmdRowBox(AppData* data, const char* json, const char*) {
    int row = 0, box = 0;
    if (!jsonFindInt(json, "row", &row) || !jsonFindInt(json, "box", &box)) return false;
    if (!validSegmentRow(*data->state, row) || box < 0) return false;
    stageEditSetBox(data, static_cast<size_t>(row), box);
    refreshSegmentList(data);
    return true;
}

static bool cmdRowSpeed(AppData* data, const char* json, const char*) {
    int row = 0; double kph = 0;
    if (!jsonFindInt(json, "row", &row) || !jsonFindDouble(json, "kph", &kph)) return false;
    if (data->state->units) kph = mphToKph(kph);   // the phone types in the box's speed units; stored in km/h
    if (!validSegmentRow(*data->state, row) || kph < 0 || kph > 400) return false;
    stageEditSetSpeed(data, static_cast<size_t>(row), kph);
    refreshSegmentList(data);
    return true;
}

static bool cmdRowChgAuto(AppData* data, const char* json, const char* type) {
    int row = 0; bool on = false;
    if (!jsonFindInt(json, "row", &row) || !jsonFindBool(json, "on", &on)) return false;
    if (!validSegmentRow(*data->state, row)) return false;
    if (strcmp(type, "row_chg") == 0) stageEditSetChange(data, static_cast<size_t>(row), on);
    else stageEditSetAuto(data, static_cast<size_t>(row), on);
    refreshSegmentList(data);
    return true;
}

static bool cmdRowTo(AppData* data, const char* json, const char*) {
    int row = 0; double to_m = 0;
    if (!jsonFindInt(json, "row", &row) || !jsonFindDouble(json, "to_m", &to_m)) return false;
    if (!validSegmentRow(*data->state, row)) return false;
    bool ok = stageEditSetRowTo(data, static_cast<size_t>(row), to_m);
    if (!ok) data->webNotice = stageEditLocked(*data->state) ? STAGE_EDIT_LOCKED_NOTICE
                             : "That end distance must be past this row's start and short of the next row's end.";
    refreshSegmentList(data);
    notifyWebStateNow(data);   // put the old figure back on a refusal
    return ok;
}

static bool cmdRowDelete(AppData* data, const char* json, const char*) {
    int row = 0;
    if (!jsonFindInt(json, "row", &row)) return false;
    if (!validSegmentRow(*data->state, row)) return false;
    stageEditDeleteRow(data, static_cast<size_t>(row));
    refreshSegmentList(data);
    return true;
}

static bool cmdStageFirst(AppData* data, const char* json, const char*) {
    int number = -1; double from_m = -1;
    jsonFindInt(json, "number", &number);
    jsonFindDouble(json, "from_m", &from_m);
    {   // The phone sends the figure its field shows, which is cut to the format in use: when that
        // is just the stored figure as shown, keep the stored one (changing the box number must
        // not shorten a programmed 8.345 to 8.34).
        const int n = stageEditSlot(data);
        const double stored = data->state->memory_slots[n - 1].first_from_m;
        const int unit = data->state->distance_unit;
        if (from_m >= 0.0 && formatDistance(from_m, unit) == formatDistance(stored, unit)) from_m = -1;
    }
    // A start not before the first end is refused (the roadbook ends win): put the old figure back, as row_to does.
    const bool ok = stageEditSetFirst(data, number, from_m);
    refreshSegmentList(data);
    if (!ok) {
        data->webNotice = stageEditLocked(*data->state) ? STAGE_EDIT_LOCKED_NOTICE
                                                        : "The start must be before the first end distance.";
        notifyWebStateNow(data);
    }
    return ok;
}

static bool cmdStageStart(AppData* data, const char* json, const char*) {
    std::string time; double dist = -1;
    long tod = -1;
    if (jsonFindStringValue(json, "time", &time) && !time.empty()) {
        tod = parseTimeOfDay(time);
        if (tod < 0) return false;
    }
    jsonFindDouble(json, "distance_m", &dist);
    {   // As above: a start time changed on the phone re-sends the distance its field shows.
        const int n = stageEditSlot(data);
        const double stored = static_cast<double>(data->state->memory_slots[n - 1].start_distance_m);
        const int unit = data->state->distance_unit;
        if (dist >= 0.0 && formatDistance(dist, unit, true) == formatDistance(stored, unit, true)) dist = -1;   // shown to the nearest
    }
    std::string notice;
    stageEditSetStart(data, tod, dist < 0 ? -1 : std::lround(dist), &notice);
    data->webNotice = notice;   // as the box's start time field flashes it
    refreshSegmentList(data);
    return true;
}

static bool cmdAutostartSet(AppData* data, const char* json, const char*) {
    bool on = false;
    if (!jsonFindBool(json, "on", &on)) return false;
    // As the box's auto key: no arming while a stage is running.
    if (on && classifyTotalReset(*data->state) == TotalResetCase::StageRunning) {
        notifyWebStateNow(data);
        return false;
    }
    std::string err;
    bool ok = stageEditSetAutostart(data, on, &err);
    if (!ok) data->webNotice = err;   // as the box's auto key flashes it
    notifyWebStateNow(data);
    return ok;
}

static bool cmdClearStageAll(AppData* data, const char*, const char* type) {
    if (strcmp(type, "clear_stage") == 0) stageEditClearStage(data);
    else stageEditClearAll(data);
    refreshSegmentList(data);
    return true;
}

static bool cmdMemoryRecall(AppData* data, const char* json, const char*) {
    int slot = 0;
    if (!jsonFindInt(json, "slot", &slot)) return false;
    if (slot < 1 || slot > RallyState::MAX_MEMORY_SLOTS) return false;
    if (stageChangeLocked(*data->state)) { notifyWebStateNow(data); return false; }   // as the box: not mid-stage
    // A programmed stage is recalled; a blank number is selected empty, ready to be
    // programmed -- the box's Choose Stage and Navi - / + (RB-NAV-64).
    loadStageNumber(data, slot);
    return true;
}

// ---- Navi keys ----

static bool cmdReverseSet(AppData* data, const char* json, const char*) {
    bool on = false;
    if (!jsonFindBool(json, "on", &on)) return false;
    setReverse(data, on);
    return true;
}

static bool cmdCounterFaultClear(AppData* data, const char*, const char*) {
    if (data->poller) data->poller->clearCounterFault();
    updateCounterFaultWidgets(data);
    notifyWebStateNow(data);
    return true;
}

static bool cmdFreezeToggle(AppData* data, const char*, const char*) { toggleHomeFreeze(data); return true; }

static bool cmdAbortStage(AppData* data, const char*, const char*) {
    // The phone asks the crew first; the box only acts if a stage can end.
    if (!stageAbortable(*data->state)) return false;
    performAbortStage(data);
    return true;
}

static bool cmdFlagNextMinute(AppData* data, const char* json, const char*) {
    if (!nextMinuteAllowed(*data->state)) return false;   // italian hides the key; refuse it here too
    // With a stage running the phone has already asked "abort the current
    // stage?" and sends abort=true on yes; without it the press is refused.
    bool abort_first = false;
    jsonFindBool(json, "abort", &abort_first);
    if (classifyTotalReset(*data->state) == TotalResetCase::StageRunning) {
        if (!abort_first) return false;
        performAbortStage(data);
    }
    startStageAtNextMinute(data);
    return true;
}

static bool cmdFlagLateArrival(AppData* data, const char*, const char*) {
    // As the box's own go key: with a stage to abort the key is "stage abort", so a go sent
    // from a phone whose figures are behind (an autostart has just fired) is refused.
    if (goKeyAborts(*data->state)) return false;
    performStageGo(data);
    return true;
}

static bool cmdDriverZero(AppData* data, const char* json, const char*) {
    bool reset = false;
    jsonFindBool(json, "reset", &reset);
    applyDriverZero(data, reset);
    return true;
}

// ---- Date / time ----

static bool cmdRallyToComputer(AppData* data, const char*, const char*) {
    data->state->rallyTimeOffset_ms = 0;
    saveConfig(data);
    return true;
}

static bool cmdRallyToEntered(AppData* data, const char* json, const char*) {
    std::string time;
    if (!jsonFindStringValue(json, "time", &time)) return false;
    long tod = parseTimeOfDay(time);
    if (tod < 0) return false;
    setRallyToEntered(data, tod);
    return true;
}

static bool cmdRallyTrim(AppData* data, const char* json, const char*) {
    int steps = 0;
    if (!jsonFindInt(json, "steps", &steps) || (steps != 1 && steps != -1)) return false;
    applyRallyTrim(data, steps);
    return true;
}

static bool cmdPhoneTime(AppData* data, const char* json, const char*) {
    double ms = 0;
    if (!jsonFindDouble(json, "ms", &ms) || ms <= 0) return false;
    data->phoneClockMs = static_cast<int64_t>(ms);
    data->phoneClockSeenMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return false;   // a heartbeat, not a change worth a state broadcast
}

static bool cmdComputerToPhone(AppData* data, const char*, const char*) {
    std::string err;
    return setComputerClockToPhone(data, &err);
}

// ---- Calibration ----

static bool cmdCalZero(AppData* data, const char*, const char*) { on_calibration_start(nullptr, data); return true; }

static bool cmdCalSensor(AppData* data, const char* json, const char*) {
    bool both = false;
    if (!jsonFindBool(json, "both", &both)) return false;
    if (both) on_set_sensor_both(nullptr, data); else on_set_sensor_1(nullptr, data);
    return true;
}

// A press of a button of the question window the box has open (item 11): either device can answer it.
static bool cmdBoxDialogAnswer(AppData*, const char* json, const char*) {
    int index = -1;
    if (!jsonFindInt(json, "index", &index)) return false;
    return answerOpenDialog(index);
}

static bool cmdCalSave(AppData* data, const char* json, const char*) {
    // The measured distance as typed, in the unit the box shows (metres, or miles in miles): the same rule the
    // box's own key applies, so box and phone convert it in one place (item 10).
    std::string typed;
    if (jsonFindStringValue(json, "text", &typed)) {
        if (typed.empty() || typed.size() > 20) return false;
        applyCalibrationDistance(data, typed.c_str());
        return true;
    }
    return false;
}

static bool cmdCalReset(AppData* data, const char* json, const char*) {
    double pulses = 0;
    if (!jsonFindDouble(json, "pulses", &pulses) || pulses <= 0) return false;
    // Whole pulses, as the box's entry is typed.
    applyCalibrationPulses(data, std::nearbyint(pulses));
    return true;
}

static bool cmdPropPulses(AppData* data, const char* json, const char*) {
    int n = 0;
    if (!jsonFindInt(json, "n", &n) || !validPropPulsesPerRev(n)) return false;
    data->state->prop_pulses_per_rev = n;
    saveConfigSoon(data);
    return true;
}

// ---- System switches on Settings ----

static bool cmdSetting(AppData* data, const char* json, const char*) {
    char name[32] = "";
    bool on = false;
    if (!jsonFindString(json, "name", name, sizeof(name)) || !jsonFindBool(json, "on", &on)) return false;
    if (strcmp(name, "units_mph") == 0) {
        if (data->state->units != on && data->unitToggleBtn) gtk_button_clicked(data->unitToggleBtn);
    } else if (strcmp(name, "distance_mode") == 0) {
        // The two Settings choices in one value (format and unit): see DIST_* in stage_rows.h.
        int mode = -1;
        if (!jsonFindInt(json, "mode", &mode) || !distUnitValid(mode)) return false;
        if (!changeDistanceUnit(*data->state, mode)) return false;   // km/m <-> miles is refused while a stage is live
        data->tickMem.beepCursorsStale = true;   // the waypoints may have been rebuilt; an idle Navigation page re-adopts them
        adoptRoadbookIfIdle(data);
        updateSettingsDisplay(data);
    } else if (strcmp(name, "measure_ab_bc") == 0) {
        // Keys on the box: AB,BC (on) / AB,AC (off).
        data->state->measure_ab_bc = on;
        updateSettingsDisplay(data);
    } else if (strcmp(name, "stage_logging") == 0) {
        data->state->stage_logging = on;
    } else if (strcmp(name, "sensor_logging") == 0) {
        data->state->debug_logs_enabled = on;
        setDebugLogging(data, on);
    } else if (strcmp(name, "force_single") == 0) {
        on_force_single_display_toggle(nullptr, on, data);
    } else {
        return false;
    }
    if (strcmp(name, "distance_mode") == 0) saveConfig(data);   // rewrites the stages
    else saveConfigSoon(data);   // a settings toggle
    return true;
}

// Rally type keys: italian 2, greek 0, surrey 1. Same refusal as the box
// while a stage is live, and the same linked distance / measure defaults.
static bool cmdRallyType(AppData* data, const char* json, const char*) {
    int n = -1;
    if (!jsonFindInt(json, "n", &n) || (n != RALLY_GREEK && n != RALLY_SURREY && n != RALLY_ITALIAN)) return false;
    if (stageEditLocked(*data->state)) return false;
    applyRallyTypeDefaults(*data->state, n);
    updateSettingsDisplay(data);
    saveConfig(data);
    return true;
}

// Reaction time offset: 0.01 s a press, clamped to the box's range.
static bool cmdReactionStep(AppData* data, const char* json, const char*) {
    int ms = 0;
    if (!jsonFindInt(json, "ms", &ms) || (ms != 10 && ms != -10)) return false;
    data->state->reaction_offset_ms = clampReactionOffsetMs(data->state->reaction_offset_ms + ms);
    updateSettingsDisplay(data);
    saveConfigSoon(data);   // a held +/- key is one write
    return true;
}

static bool cmdBeepSet(AppData* data, const char* json, const char*) {
    // One command for the whole Beep Assist panel: the phone sends only
    // the fields the operator actually changed, so each is optional.
    bool flag = false;
    double value = 0.0;
    // Italian greys these on the box; a phone press must not turn them on.
    if (!greekBeepSettingsLocked(*data->state)) {
        if (jsonFindBool(json, "navigation", &flag)) data->state->beep_navigation_mode = flag;
        if (jsonFindBool(json, "timing", &flag)) data->state->beep_timing_mode = flag;
    }
    // Nero2.0 has no separate master: Beep Assist is on when either mode is.
    data->state->beep_assist_enabled =
        data->state->beep_navigation_mode || data->state->beep_timing_mode;
    if (!greekBeepSettingsLocked(*data->state)) {
        if (jsonFindDouble(json, "advance_m", &value) && value >= 0.0) {
            data->state->beep_advance_m = value;
        }
        if (jsonFindDouble(json, "advance_s", &value) && value >= 0.0) {
            data->state->beep_advance_s = value;
        }
    }
    // The beep distances come from the Segments rows now (every row's end),
    // so the phone no longer sends a waypoint list.
    // Anything here can move a waypoint or re-arm a mode, so the beep
    // bookmarks are rebuilt from where the car actually is on the next
    // display tick rather than replaying what is already behind it.
    data->tickMem.beepCursorsStale = true;
    saveConfigSoon(data);
    return true;
}

static bool cmdGaugeRange(AppData* data, const char* json, const char*) {
    int r = 0;
    if (!jsonFindInt(json, "seconds", &r) || r < 1 || r > 3) return false;
    data->state->gauge_range_s = r;
    saveConfigSoon(data);
    return true;
}

static bool cmdToneSet(AppData* data, const char* json, const char*) {
    bool flag = false;
    int tone_type = 0;
    if (jsonFindBool(json, "enabled", &flag)) data->state->tone_enabled = flag;
    if (jsonFindInt(json, "tone_type", &tone_type)) {
        if (!validToneType(tone_type)) return false;
        data->state->tone_type_behind = data->state->tone_type_ahead = tone_type;
    }
    if (jsonFindInt(json, "tone_type_behind", &tone_type)) {
        if (!validToneType(tone_type)) return false;
        data->state->tone_type_behind = tone_type;
    }
    if (jsonFindInt(json, "tone_type_ahead", &tone_type)) {
        if (!validToneType(tone_type)) return false;
        data->state->tone_type_ahead = tone_type;
    }
    saveConfigSoon(data);
    return true;
}

// Phone upload stage. Check shows what the file says; load writes it.
static bool cmdStageUploadCheck(AppData* data, const char* json, const char*) {
    std::string id, text;
    if (!jsonFindEscapedString(json, "id", &id) || id.empty()) return false;
    if (!jsonFindEscapedString(json, "text", &text)) return false;
    data->uploadCheckId = id;
    data->uploadCheckText = text;
    data->uploadLoaded.clear();
    return true;
}

static bool cmdStageUploadLoad(AppData* data, const char* json, const char*) {
    std::string id;
    bool replace = false;
    if (!jsonFindEscapedString(json, "id", &id) || id != data->uploadCheckId || data->uploadCheckId.empty())
        return false;
    jsonFindBool(json, "replace", &replace);
    const StageFile file = parseStageFile(data->uploadCheckText);
    if (!uploadRefusal(*data->state, file).empty()) return true;
    if (!uploadOverwrites(*data->state, file).empty() && !replace) return true;
    data->uploadLoaded = stageUploadCommit(data, file);
    data->uploadCheckId.clear();
    data->uploadCheckText.clear();
    return true;
}

static bool cmdSectorType(AppData* data, const char* json, const char*) {
    int row = -1;
    if (!jsonFindInt(json, "row", &row) || row < 0) return false;
    sectorEditStepType(data, static_cast<size_t>(row));
    updateSegmentsDisplay(data);
    return true;
}

static bool cmdSectorRef(AppData* data, const char* json, const char*) {
    int row = -1, ref = -1;
    if (!jsonFindInt(json, "row", &row) || !jsonFindInt(json, "ref", &ref) || row < 0) return false;
    sectorEditSetRef(data, static_cast<size_t>(row), ref);
    updateSegmentsDisplay(data);
    return true;
}

static bool cmdSectorPc(AppData* data, const char* json, const char*) {
    int row = -1;
    char text[32];
    if (!jsonFindInt(json, "row", &row) || row < 0) return false;
    if (!jsonFindString(json, "text", text, sizeof(text))) return false;
    const long s = parseDurationSeconds(text);
    if (s > 0) sectorEditSetPcDuration(data, static_cast<size_t>(row), static_cast<double>(s));
    updateSegmentsDisplay(data);
    return true;
}

static bool cmdSectorDuration(AppData* data, const char* json, const char*) {
    char text[32];
    if (!jsonFindString(json, "text", text, sizeof(text))) return false;
    const long s = parseDurationSeconds(text);
    if (s > 0 || text[0] == '\0') sectorEditSetSectorDuration(data, s > 0 ? s : -1);
    updateSegmentsDisplay(data);
    return true;
}

static bool cmdBoxSetup(AppData* data, const char* json, const char*) {
    char name[32];
    if (!jsonFindString(json, "name", name, sizeof(name))) return false;
    if (strcmp(name, "reset_layout") == 0) phoneResetLayout(data);
    else if (strcmp(name, "bt_connect") == 0) phoneBtConnect(data);
    else if (strcmp(name, "bt_disconnect") == 0) phoneBtDisconnect(data);
    else if (strcmp(name, "wifi_list") == 0) phoneWifiList(data);
    else if (strcmp(name, "displays") == 0) phoneDisplaysList(data);
    else if (strcmp(name, "remember_speaker") == 0) phoneRememberSpeaker(data);
    else if (strcmp(name, "update") == 0) phoneUpdate(data);
    else if (strcmp(name, "rotate") == 0 || strcmp(name, "keyboard") == 0) {
        char output[64];
        if (!jsonFindString(json, "output", output, sizeof(output))) return false;
        if (strcmp(name, "rotate") == 0) phoneRotate(data, output);
        else phoneKeyboard(data, output);
    }
    else if (strcmp(name, "wifi_use") == 0) {
        // Escape-aware: a network name may hold a quote or a backslash, which the phone sends escaped.
        std::string net;
        if (!jsonFindEscapedString(json, "net", &net) || net.empty() || net.size() > 127) return false;
        phoneWifiUse(data, net);
    } else return false;
    return true;
}

static bool cmdStageUploadCancel(AppData* data, const char*, const char*) {
    data->uploadCheckId.clear();
    data->uploadCheckText.clear();
    data->uploadLoaded.clear();
    return true;
}

// Every command the phone can send, by name. An unlisted name is refused (false), as it always was.
static const struct { const char* name; CommandHandler handler; } COMMANDS[] = {
    {"phone_lead_step", cmdPhoneLeadStep},
    {"reset_trip", cmdResetTrip},
    {"reset_total", cmdResetTotal},
    {"reset_total_capture", cmdResetTotalCapture},
    {"reset_total_cancel", cmdResetTotalCancel},
    {"distance_adjust", cmdDistanceAdjust},
    {"distance_set", cmdDistanceSet},
    {"navi_set", cmdNaviSet},
    {"next", cmdNextPrev},
    {"prev", cmdNextPrev},
    {"edit_select", cmdEditSelect},
    {"rows_add", cmdRowsAdd},
    {"row_box", cmdRowBox},
    {"row_speed", cmdRowSpeed},
    {"row_chg", cmdRowChgAuto},
    {"row_auto", cmdRowChgAuto},
    {"row_to", cmdRowTo},
    {"row_delete", cmdRowDelete},
    {"stage_first", cmdStageFirst},
    {"stage_start", cmdStageStart},
    {"autostart_set", cmdAutostartSet},
    {"clear_stage", cmdClearStageAll},
    {"clear_all", cmdClearStageAll},
    {"memory_recall", cmdMemoryRecall},
    {"reverse_set", cmdReverseSet},
    {"counter_fault_clear", cmdCounterFaultClear},
    {"freeze_toggle", cmdFreezeToggle},
    {"abort_stage", cmdAbortStage},
    {"flag_next_minute", cmdFlagNextMinute},
    {"flag_late_arrival", cmdFlagLateArrival},
    {"driver_zero", cmdDriverZero},
    {"rally_to_computer", cmdRallyToComputer},
    {"rally_to_entered", cmdRallyToEntered},
    {"rally_trim", cmdRallyTrim},
    {"phone_time", cmdPhoneTime},
    {"computer_to_phone", cmdComputerToPhone},
    {"cal_zero", cmdCalZero},
    {"cal_sensor", cmdCalSensor},
    {"box_dialog_answer", cmdBoxDialogAnswer},
    {"cal_save", cmdCalSave},
    {"cal_reset", cmdCalReset},
    {"prop_pulses", cmdPropPulses},
    {"setting", cmdSetting},
    {"rally_type", cmdRallyType},
    {"reaction_step", cmdReactionStep},
    {"beep_set", cmdBeepSet},
    {"gauge_range", cmdGaugeRange},
    {"tone_set", cmdToneSet},
    {"stage_upload_check", cmdStageUploadCheck},
    {"stage_upload_load", cmdStageUploadLoad},
    {"sector_type", cmdSectorType},
    {"sector_ref", cmdSectorRef},
    {"sector_pc", cmdSectorPc},
    {"sector_duration", cmdSectorDuration},
    {"box_setup", cmdBoxSetup},
    {"stage_upload_cancel", cmdStageUploadCancel},
};

bool webHandleCommand(AppData* data, const char* json) {
    if (!data || !json) return false;

    char type[64];
    if (!jsonFindString(json, "type", type, sizeof(type))) return false;

    for (const auto& c : COMMANDS)
        if (strcmp(type, c.name) == 0) return c.handler(data, json, type);
    return false;
}
