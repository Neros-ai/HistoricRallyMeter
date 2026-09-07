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
        ConfigFile::save(*data->state);
        refreshSegmentList(data);
        return true;
    }
    if (strcmp(type, "segment_delete") == 0) {
        int index = 0;
        if (!jsonFindInt(json, "index", &index)) return false;
        if (index < 0 || index >= static_cast<int>(data->state->segments.size())) return false;
        data->state->segments.erase(data->state->segments.begin() + index);
        if (data->state->segment_current_number >= static_cast<long>(data->state->segments.size())) {
            data->state->segment_current_number =
                data->state->segments.empty() ? -1 : static_cast<long>(data->state->segments.size()) - 1;
        }
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
        ConfigFile::save(*data->state);
        return true;
    }
    if (strcmp(type, "memory_recall") == 0) {
        int slot = 0;
        if (!jsonFindInt(json, "slot", &slot)) return false;
        if (slot < 1 || slot > RallyState::MAX_MEMORY_SLOTS) return false;
        if (data->state->memory_slots[slot - 1].empty()) return false;
        data->state->segments = data->state->memory_slots[slot - 1].segments;
        data->state->beep_waypoints_m = data->state->memory_slots[slot - 1].beep_waypoints_m;
        data->beepCursorsStale = true;
        data->state->segment_current_number = data->state->segments.empty() ? -1 : 0;
        ConfigFile::save(*data->state);
        refreshSegmentList(data);
        return true;
    }
    if (strcmp(type, "beep_set") == 0) {
        // One command for the whole Beep Assist panel: the phone sends only
        // the fields the operator actually changed, so each is optional.
        bool flag = false;
        double value = 0.0;
        char waypoints[512];
        if (jsonFindBool(json, "enabled", &flag)) data->state->beep_assist_enabled = flag;
        if (jsonFindBool(json, "navigation", &flag)) data->state->beep_navigation_mode = flag;
        if (jsonFindBool(json, "timing", &flag)) data->state->beep_timing_mode = flag;
        if (jsonFindDouble(json, "advance_m", &value) && value >= 0.0) {
            data->state->beep_advance_m = value;
        }
        if (jsonFindDouble(json, "advance_s", &value) && value >= 0.0) {
            data->state->beep_advance_s = value;
        }
        if (jsonFindString(json, "waypoints_km", waypoints, sizeof(waypoints))) {
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
