#include "web_commands.h"
#include "config_file.h"
#include "calculations.h"
#include "callbacks.h"
#include "rally_types.h"
#include "rally_state.h"
#include "stage_edit.h"
#include "stage_rows.h"
#include "ui_home.h"
#include "ui_datetime.h"
#include "ui_calibration.h"
#include <chrono>

#include <cctype>
#include <cstring>
#include <string>

static const char* jsonFindString(const char* json, const char* key, char* out, size_t out_len) {
    std::string needle = std::string("\"") + key + "\":\"";
    const char* p = strstr(json, needle.c_str());
    if (!p) return nullptr;
    p += needle.size();
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
    std::string needle = std::string("\"") + key + "\":\"";
    const char* p = strstr(json, needle.c_str());
    if (!p) return false;
    p += needle.size();
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
    std::string needle = std::string("\"") + key + "\":";
    const char* p = strstr(json, needle.c_str());
    if (!p) return false;
    p += needle.size();
    while (*p && isspace(static_cast<unsigned char>(*p))) p++;
    if (strncmp(p, "true", 4) == 0) { *out = true; return true; }
    if (strncmp(p, "false", 5) == 0) { *out = false; return true; }
    return false;
}

static bool jsonFindInt(const char* json, const char* key, int* out) {
    std::string needle = std::string("\"") + key + "\":";
    const char* p = strstr(json, needle.c_str());
    if (!p) return false;
    p += needle.size();
    *out = static_cast<int>(strtol(p, nullptr, 10));
    return true;
}

static bool jsonFindDouble(const char* json, const char* key, double* out) {
    std::string needle = std::string("\"") + key + "\":";
    const char* p = strstr(json, needle.c_str());
    if (!p) return false;
    p += needle.size();
    *out = strtod(p, nullptr);
    return true;
}

bool webHandleCommand(AppData* data, const char* json) {
    if (!data || !json) return false;

    char type[64];
    if (!jsonFindString(json, "type", type, sizeof(type))) return false;

    if (strcmp(type, "reset_trip") == 0) {
        on_trip_reset(nullptr, data);
        return true;
    }
    if (strcmp(type, "reset_total") == 0) {
        // With a stage running the phone sends "reset_total_capture" on the
        // press, asks the box's question, and sends choice "confirm" only if
        // the crew confirm. If the stage was driven out meanwhile, the confirm
        // is the plain idle reset, as on the box.
        char choice[16] = "";
        jsonFindString(json, "choice", choice, sizeof(choice));
        return applyTotalReset(data, strcmp(choice, "confirm") == 0
                                         ? TotalResetChoice::ConfirmDistanceReset
                                         : TotalResetChoice::Cancel);
    }
    if (strcmp(type, "reset_total_capture") == 0) {
        // The press itself: the distance zero, if the crew then confirm.
        if (classifyTotalReset(*data->state) == TotalResetCase::StageRunning) {
            captureTotalResetPress(data);
        }
        return false;
    }
    if (strcmp(type, "reset_total_cancel") == 0) {
        // The crew answered Back on the phone. Drops the captured press and
        // does nothing else -- see cancelTotalResetPress for why this is its
        // own message rather than a reset_total.
        cancelTotalResetPress(data);
        return false;
    }
    if (strcmp(type, "distance_adjust") == 0) {
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
    if (strcmp(type, "distance_set") == 0) {
        double meters = 0.0;
        if (!jsonFindDouble(json, "meters", &meters)) return false;
        // Six digits, matching the entry cap on the box's dialog: past
        // 999,999 m the display switches to km and reads as truncated.
        if (meters < 0.0 || meters > 999999.0) return false;
        return applyDistanceSet(data, meters);
    }
    if (strcmp(type, "next") == 0 || strcmp(type, "prev") == 0) {
        if (strcmp(type, "next") == 0) on_next_press(nullptr, data);
        else on_prev_press(nullptr, data);
        return true;
    }
    // ---- the stage on show (Segments) -- the same calls the box's page makes ----
    if (strcmp(type, "edit_select") == 0) {
        int slot = 0;
        if (!jsonFindInt(json, "slot", &slot) || slot < 1 || slot > RallyState::MAX_MEMORY_SLOTS) return false;
        stageEditSelect(data, slot);
        refreshSegmentList(data);
        return true;
    }
    if (strcmp(type, "rows_add") == 0) {
        std::string text;
        if (!jsonFindStringValue(json, "text", &text)) return false;
        std::string rest;
        int added = stageEditAddDistances(data, text, &rest);
        refreshSegmentList(data);
        return added > 0 && rest.empty();
    }
    if (strcmp(type, "row_speed") == 0) {
        int row = 0; double kph = 0;
        if (!jsonFindInt(json, "row", &row) || !jsonFindDouble(json, "kph", &kph)) return false;
        if (row < 0 || row >= static_cast<int>(data->state->segments.size()) || kph < 0 || kph > 400) return false;
        stageEditSetSpeed(data, static_cast<size_t>(row), kph);
        refreshSegmentList(data);
        return true;
    }
    if (strcmp(type, "row_chg") == 0 || strcmp(type, "row_auto") == 0) {
        int row = 0; bool on = false;
        if (!jsonFindInt(json, "row", &row) || !jsonFindBool(json, "on", &on)) return false;
        if (row < 0 || row >= static_cast<int>(data->state->segments.size())) return false;
        if (strcmp(type, "row_chg") == 0) stageEditSetChange(data, static_cast<size_t>(row), on);
        else stageEditSetAuto(data, static_cast<size_t>(row), on);
        refreshSegmentList(data);
        return true;
    }
    if (strcmp(type, "row_delete") == 0) {
        int row = 0;
        if (!jsonFindInt(json, "row", &row)) return false;
        if (row < 0 || row >= static_cast<int>(data->state->segments.size())) return false;
        stageEditDeleteRow(data, static_cast<size_t>(row));
        refreshSegmentList(data);
        return true;
    }
    if (strcmp(type, "stage_first") == 0) {
        int number = -1; double from_m = -1;
        jsonFindInt(json, "number", &number);
        jsonFindDouble(json, "from_m", &from_m);
        stageEditSetFirst(data, number, from_m);
        refreshSegmentList(data);
        return true;
    }
    if (strcmp(type, "stage_start") == 0) {
        std::string time; double dist = -1;
        long tod = -1;
        if (jsonFindStringValue(json, "time", &time) && !time.empty()) {
            tod = parseTimeOfDay(time);
            if (tod < 0) return false;
        }
        jsonFindDouble(json, "distance_m", &dist);
        stageEditSetStart(data, tod, static_cast<long>(dist));
        refreshSegmentList(data);
        return true;
    }
    if (strcmp(type, "autostart_set") == 0) {
        bool on = false;
        if (!jsonFindBool(json, "on", &on)) return false;
        std::string err;
        bool ok = stageEditSetAutostart(data, on, &err);
        notifyWebStateNow(data);
        return ok;
    }
    if (strcmp(type, "clear_stage") == 0 || strcmp(type, "clear_all") == 0) {
        if (strcmp(type, "clear_stage") == 0) stageEditClearStage(data);
        else stageEditClearAll(data);
        refreshSegmentList(data);
        return true;
    }
    if (strcmp(type, "memory_recall") == 0) {
        int slot = 0;
        if (!jsonFindInt(json, "slot", &slot)) return false;
        if (slot < 1 || slot > RallyState::MAX_MEMORY_SLOTS) return false;
        if (data->state->memory_slots[slot - 1].empty()) return false;
        recallMemorySlot(data, slot);
        return true;
    }

    // ---- Navi keys ----
    if (strcmp(type, "reverse_set") == 0) {
        bool on = false;
        if (!jsonFindBool(json, "on", &on)) return false;
        setReverse(data, on);
        return true;
    }
    if (strcmp(type, "abort_stage") == 0) {
        // The phone asks the crew first; the box only acts if a stage can end.
        if (!stageAbortable(*data->state)) return false;
        performAbortStage(data);
        return true;
    }
    if (strcmp(type, "flag_next_minute") == 0) {
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
    if (strcmp(type, "flag_late_arrival") == 0) { performStageGo(data); return true; }
    if (strcmp(type, "driver_zero") == 0) {
        bool reset = false;
        jsonFindBool(json, "reset", &reset);
        applyDriverZero(data, reset);
        return true;
    }

    // ---- Date / time ----
    if (strcmp(type, "rally_to_computer") == 0) {
        data->state->rallyTimeOffset_ms = 0;
        ConfigFile::save(*data->state);
        return true;
    }
    if (strcmp(type, "rally_to_entered") == 0) {
        std::string time;
        if (!jsonFindStringValue(json, "time", &time)) return false;
        long tod = parseTimeOfDay(time);
        if (tod < 0) return false;
        setRallyToEntered(data, tod);
        return true;
    }
    if (strcmp(type, "rally_trim") == 0) {
        int steps = 0;
        if (!jsonFindInt(json, "steps", &steps) || (steps != 1 && steps != -1)) return false;
        applyRallyTrim(data, steps);
        return true;
    }
    if (strcmp(type, "phone_time") == 0) {
        double ms = 0;
        if (!jsonFindDouble(json, "ms", &ms) || ms <= 0) return false;
        data->phoneClockMs = static_cast<int64_t>(ms);
        data->phoneClockSeenMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return false;   // a heartbeat, not a change worth a state broadcast
    }
    if (strcmp(type, "computer_to_phone") == 0) {
        std::string err;
        return setComputerClockToPhone(data, &err);
    }

    // ---- Calibration ----
    if (strcmp(type, "cal_zero") == 0) { on_calibration_start(nullptr, data); return true; }
    if (strcmp(type, "cal_sensor") == 0) {
        bool both = false;
        if (!jsonFindBool(json, "both", &both)) return false;
        if (both) on_set_sensor_both(nullptr, data); else on_set_sensor_1(nullptr, data);
        return true;
    }
    if (strcmp(type, "cal_save") == 0) {
        double meters = 0;
        if (!jsonFindDouble(json, "meters", &meters)) return false;
        if (meters < 500 || meters > 100000) return false;
        char buf[24];
        snprintf(buf, sizeof(buf), "%ld", static_cast<long>(meters));
        gtk_entry_set_text(data->rallyDistEntry, buf);
        on_save_calibration(nullptr, data);
        return true;
    }
    if (strcmp(type, "cal_reset") == 0) {
        double pulses = 0;
        if (!jsonFindDouble(json, "pulses", &pulses) || pulses <= 0) return false;
        char buf[32];
        snprintf(buf, sizeof(buf), "%.0f", pulses);
        gtk_entry_set_text(data->resetPulsesEntry, buf);
        on_reset_calibration_pulses(nullptr, data);
        return true;
    }
    if (strcmp(type, "prop_pulses") == 0) {
        int n = 0;
        if (!jsonFindInt(json, "n", &n) || !validPropPulsesPerRev(n)) return false;
        data->state->prop_pulses_per_rev = n;
        ConfigFile::save(*data->state);
        return true;
    }

    // ---- System switches on Settings ----
    if (strcmp(type, "setting") == 0) {
        char name[32] = "";
        bool on = false;
        if (!jsonFindString(json, "name", name, sizeof(name)) || !jsonFindBool(json, "on", &on)) return false;
        if (strcmp(name, "units_mph") == 0) {
            if (data->state->units != on && data->unitToggleBtn) gtk_button_clicked(data->unitToggleBtn);
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
        ConfigFile::save(*data->state);
        return true;
    }

    if (strcmp(type, "beep_set") == 0) {
        // One command for the whole Beep Assist panel: the phone sends only
        // the fields the operator actually changed, so each is optional.
        bool flag = false;
        double value = 0.0;
        if (jsonFindBool(json, "navigation", &flag)) data->state->beep_navigation_mode = flag;
        if (jsonFindBool(json, "timing", &flag)) data->state->beep_timing_mode = flag;
        // Nero2.0 has no separate master: Beep Assist is on when either mode is.
        data->state->beep_assist_enabled =
            data->state->beep_navigation_mode || data->state->beep_timing_mode;
        if (jsonFindDouble(json, "advance_m", &value) && value >= 0.0) {
            data->state->beep_advance_m = value;
        }
        if (jsonFindDouble(json, "advance_s", &value) && value >= 0.0) {
            data->state->beep_advance_s = value;
        }
        // The beep distances come from the Segments rows now (every row's end),
        // so the phone no longer sends a waypoint list.
        // Anything here can move a waypoint or re-arm a mode, so the beep
        // bookmarks are rebuilt from where the car actually is on the next
        // display tick rather than replaying what is already behind it.
        data->beepCursorsStale = true;
        ConfigFile::save(*data->state);
        return true;
    }
    if (strcmp(type, "tone_set") == 0) {
        bool flag = false;
        int tone_type = 0;
        if (jsonFindBool(json, "enabled", &flag)) data->state->tone_enabled = flag;
        if (jsonFindInt(json, "tone_type", &tone_type)) {
            if (tone_type != 1 && tone_type != 2) return false;
            data->state->tone_type_behind = data->state->tone_type_ahead = tone_type;
        }
        if (jsonFindInt(json, "tone_type_behind", &tone_type)) {
            if (tone_type != 1 && tone_type != 2) return false;
            data->state->tone_type_behind = tone_type;
        }
        if (jsonFindInt(json, "tone_type_ahead", &tone_type)) {
            if (tone_type != 1 && tone_type != 2) return false;
            data->state->tone_type_ahead = tone_type;
        }
        ConfigFile::save(*data->state);
        return true;
    }
    return false;
}
