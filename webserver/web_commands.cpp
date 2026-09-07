#include "web_commands.h"
#include "config_file.h"
#include "calculations.h"
#include "callbacks.h"
#include "rally_types.h"
#include "rally_state.h"

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

// Whole-value form, with no length cap. The waypoint list can outgrow any
// fixed buffer, and a truncated list is worse than none: the cut lands
// mid-number, so the fragment parses as a real waypoint the operator never
// entered and every waypoint after it is silently dropped.
static bool jsonFindStringValue(const char* json, const char* key, std::string* out) {
    std::string needle = std::string("\"") + key + "\":\"";
    const char* p = strstr(json, needle.c_str());
    if (!p) return false;
    p += needle.size();
    out->clear();
    while (*p && *p != '"') out->push_back(*p++);
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
        on_total_reset(nullptr, data);
        return true;
    }
    if (strcmp(type, "next_prev") == 0) {
        on_next_prev_segment(nullptr, data);
        return true;
    }
    if (strcmp(type, "segment_set") == 0) {
        int index = 0;
        double kph = 0, meters = 0;
        bool autoNext = true;
        if (!jsonFindInt(json, "index", &index)) return false;
        if (!jsonFindDouble(json, "target_speed_kph", &kph)) return false;
        if (!jsonFindDouble(json, "distance_m", &meters)) return false;
        jsonFindBool(json, "autoNext", &autoNext);
        if (index < 0 || index >= static_cast<int>(data->state->segments.size())) return false;
        if (kph <= 0 || meters <= 0) return false;

        Segment& seg = data->state->segments[index];
        seg.target_speed_kph = kph;
        seg.target_speed_counts_per_hour = kphToCountsPerHour(kph, data->state->calibration);
        seg.distance_m = meters;
        seg.distance_counts = (meters * 1e6) / data->state->calibration;
        seg.autoNext = autoNext;
        data->state->last_memory_slot = 0;
        adoptRoadbookIfIdle(data);
        ConfigFile::save(*data->state);
        refreshSegmentList(data);
        return true;
    }
    if (strcmp(type, "segment_add") == 0) {
        double kph = 0, meters = 0;
        bool autoNext = true;
        if (!jsonFindDouble(json, "target_speed_kph", &kph)) return false;
        if (!jsonFindDouble(json, "distance_m", &meters)) return false;
        jsonFindBool(json, "autoNext", &autoNext);
        if (kph <= 0 || meters <= 0) return false;

        Segment seg;
        seg.target_speed_kph = kph;
        seg.target_speed_counts_per_hour = kphToCountsPerHour(kph, data->state->calibration);
        seg.distance_m = meters;
        seg.distance_counts = (meters * 1e6) / data->state->calibration;
        seg.autoNext = autoNext;
        data->state->segments.push_back(seg);
        data->state->last_memory_slot = 0;
        adoptRoadbookIfIdle(data);
        ConfigFile::save(*data->state);
        refreshSegmentList(data);
        return true;
    }
    if (strcmp(type, "segment_delete") == 0) {
        int index = 0;
        if (!jsonFindInt(json, "index", &index)) return false;
        if (index < 0 || index >= static_cast<int>(data->state->segments.size())) return false;
        // segment_current_number indexes the running stage's snapshot, not
        // this list, so a running stage is left alone.
        data->state->segments.erase(data->state->segments.begin() + index);
        data->state->last_memory_slot = 0;
        adoptRoadbookIfIdle(data);
        ConfigFile::save(*data->state);
        refreshSegmentList(data);
        return true;
    }
    if (strcmp(type, "memory_store") == 0) {
        int slot = 0;
        if (!jsonFindInt(json, "slot", &slot)) return false;
        if (slot < 1 || slot > RallyState::MAX_MEMORY_SLOTS) return false;
        data->state->memory_slots[slot - 1].segments = data->state->segments;
        data->state->memory_slots[slot - 1].beep_waypoints_m = data->state->beep_waypoints_m;
        data->state->memory_slots[slot - 1].waypoints_recorded = true;
        ConfigFile::save(*data->state);
        return true;
    }
    if (strcmp(type, "memory_recall") == 0) {
        int slot = 0;
        if (!jsonFindInt(json, "slot", &slot)) return false;
        if (slot < 1 || slot > RallyState::MAX_MEMORY_SLOTS) return false;
        if (data->state->memory_slots[slot - 1].empty()) return false;
        data->state->segments = data->state->memory_slots[slot - 1].segments;
        data->state->last_memory_slot = slot;
        adoptRoadbookIfIdle(data);
        // A slot from a pre-Beep-Assist config has no list of its own; the
        // live one stays rather than being silently deleted.
        if (data->state->memory_slots[slot - 1].waypoints_recorded) {
            data->state->beep_waypoints_m = data->state->memory_slots[slot - 1].beep_waypoints_m;
        }
        data->beepCursorsStale = true;
        ConfigFile::save(*data->state);
        refreshSegmentList(data);
        // Same pair as the GTK recall path: without this the stage-setup text
        // view still shows the previous stage, and the next keystroke there
        // commits that stale text back over the list just recalled.
        refreshBeepWaypointView(data);
        return true;
    }
    if (strcmp(type, "beep_set") == 0) {
        // One command for the whole Beep Assist panel: the phone sends only
        // the fields the operator actually changed, so each is optional.
        bool flag = false;
        double value = 0.0;
        if (jsonFindBool(json, "enabled", &flag)) data->state->beep_assist_enabled = flag;
        if (jsonFindBool(json, "navigation", &flag)) data->state->beep_navigation_mode = flag;
        if (jsonFindBool(json, "timing", &flag)) data->state->beep_timing_mode = flag;
        if (jsonFindDouble(json, "advance_m", &value) && value >= 0.0) {
            data->state->beep_advance_m = value;
        }
        if (jsonFindDouble(json, "advance_s", &value) && value >= 0.0) {
            data->state->beep_advance_s = value;
        }
        std::string waypoints;
        if (jsonFindStringValue(json, "waypoints_km", &waypoints)) {
            data->state->beep_waypoints_m = parseBeepWaypointsKm(waypoints);
            refreshBeepWaypointView(data);
        }
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
            data->state->simple_tone_mode = (tone_type == 2);
        }
        ConfigFile::save(*data->state);
        return true;
    }
    return false;
}
