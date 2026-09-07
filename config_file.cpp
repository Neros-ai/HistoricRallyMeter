#include "config_file.h"
#include "rally_state.h"
#include <fstream>
#include <string>
#include <sstream>
#include <iomanip>
#include <stdexcept>

static int64_t extractLong(const std::string& line) {
    size_t pos = line.find(':');
    if (pos != std::string::npos) {
        std::string val = line.substr(pos + 1);
        // Remove trailing comma if present
        size_t comma = val.find(',');
        if (comma != std::string::npos) val = val.substr(0, comma);
        return std::stoll(val);
    }
    return 0;
}

static double extractDouble(const std::string& line) {
    size_t pos = line.find(':');
    if (pos != std::string::npos) {
        std::string val = line.substr(pos + 1);
        // Remove trailing comma if present
        size_t comma = val.find(',');
        if (comma != std::string::npos) val = val.substr(0, comma);
        return std::stod(val);
    }
    return 0.0;
}

static bool extractBool(const std::string& line) {
    return line.find("true") != std::string::npos;
}

// Read a JSON array of plain numbers, one per line, in the same line-oriented
// style as parseSegmentArray().
static void parseDoubleArray(std::istringstream& stream, std::vector<double>& out) {
    out.clear();
    std::string line;
    while (std::getline(stream, line)) {
        if (line.find(']') != std::string::npos) return;
        try {
            size_t first = line.find_first_of("-0123456789");
            if (first == std::string::npos) continue;
            out.push_back(std::stod(line.substr(first)));
        } catch (const std::exception&) {
            continue;
        }
    }
}

static void parseSegmentArray(std::istringstream& stream, std::vector<Segment>& out, long calibration) {
    out.clear();
    std::string line;
    bool in_obj = false;
    Segment seg{};
    bool has_kph = false, has_m = false;
    
    while (std::getline(stream, line)) {
        if (line.find('{') != std::string::npos) {
            in_obj = true;
            seg = Segment{};
            has_kph = false;
            has_m = false;
        } else if (line.find('}') != std::string::npos && in_obj) {
            if (!has_kph && calibration > 0) {
                seg.target_speed_kph = (seg.target_speed_counts_per_hour * calibration) / 1e9;
            }
            if (!has_m && calibration > 0) {
                seg.distance_m = (seg.distance_counts * calibration) / 1e6;
            }
            out.push_back(seg);
            in_obj = false;
        } else if (line.find(']') != std::string::npos) {
            return;
        } else if (in_obj) {
            if (line.find("\"target_speed_kph\"") != std::string::npos) {
                seg.target_speed_kph = extractDouble(line);
                has_kph = true;
            } else if (line.find("\"target_speed_counts_per_hour\"") != std::string::npos) {
                seg.target_speed_counts_per_hour = extractDouble(line);
            } else if (line.find("\"distance_m\"") != std::string::npos) {
                seg.distance_m = extractDouble(line);
                has_m = true;
            } else if (line.find("\"distance_counts\"") != std::string::npos) {
                seg.distance_counts = extractDouble(line);
            } else if (line.find("\"autoNext\"") != std::string::npos) {
                seg.autoNext = extractBool(line);
            }
        }
    }
}

void ConfigFile::load(RallyState& state, const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return;  // Use defaults
    }
    
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();
    
    std::istringstream stream(content);
    std::string line;

    // load() takes state by reference and only assigns keys the file actually
    // contains, so a vector field is otherwise left holding whatever was
    // there before. Clear it up front: a config predating Beep Assist must
    // not leave a previous rally's waypoints in place alongside this file's
    // other values.
    state.beep_waypoints_m.clear();
    for (int i = 0; i < RallyState::MAX_MEMORY_SLOTS; i++) {
        state.memory_slots[i].beep_waypoints_m.clear();
    }

    while (std::getline(stream, line)) {
        if (line.find("\"segments\"") != std::string::npos && line.find("\"memory_") == std::string::npos) {
            parseSegmentArray(stream, state.segments, state.calibration);
        } else if (line.find("\"memory_1\"") != std::string::npos) {
            parseSegmentArray(stream, state.memory_slots[0].segments, state.calibration);
        } else if (line.find("\"memory_2\"") != std::string::npos) {
            parseSegmentArray(stream, state.memory_slots[1].segments, state.calibration);
        } else if (line.find("\"memory_3\"") != std::string::npos) {
            parseSegmentArray(stream, state.memory_slots[2].segments, state.calibration);
        } else if (line.find("\"memory_4\"") != std::string::npos) {
            parseSegmentArray(stream, state.memory_slots[3].segments, state.calibration);
        } else if (line.find("\"memory_5\"") != std::string::npos) {
            parseSegmentArray(stream, state.memory_slots[4].segments, state.calibration);
        } else if (line.find("\"units\"") != std::string::npos) {
            state.units = extractBool(line);
        } else if (line.find("\"calibration\"") != std::string::npos) {
            state.calibration = extractLong(line);
        } else if (line.find("\"counters\"") != std::string::npos) {
            state.counters = extractBool(line);
        } else if (line.find("\"total_start_cntr1\"") != std::string::npos) {
            state.total_start_cntr1 = static_cast<uint32_t>(extractLong(line));
        } else if (line.find("\"total_start_cntr2\"") != std::string::npos) {
            state.total_start_cntr2 = static_cast<uint32_t>(extractLong(line));
        } else if (line.find("\"total_start_time_ms\"") != std::string::npos) {
            state.total_start_time_ms = extractLong(line);
        } else if (line.find("\"trip_start_cntr1\"") != std::string::npos) {
            state.trip_start_cntr1 = static_cast<uint32_t>(extractLong(line));
        } else if (line.find("\"trip_start_cntr2\"") != std::string::npos) {
            state.trip_start_cntr2 = static_cast<uint32_t>(extractLong(line));
        } else if (line.find("\"trip_start_time_ms\"") != std::string::npos) {
            state.trip_start_time_ms = extractLong(line);
        } else if (line.find("\"segment_start_cntr1\"") != std::string::npos) {
            state.segment_start_cntr1 = static_cast<uint32_t>(extractLong(line));
        } else if (line.find("\"segment_start_cntr2\"") != std::string::npos) {
            state.segment_start_cntr2 = static_cast<uint32_t>(extractLong(line));
        } else if (line.find("\"segment_start_time_ms\"") != std::string::npos) {
            state.segment_start_time_ms = extractLong(line);
        } else if (line.find("\"segment_current_number\"") != std::string::npos) {
            state.segment_current_number = static_cast<int32_t>(extractLong(line));
        } else if (line.find("\"rallyTimeOffset_ms\"") != std::string::npos) {
            state.rallyTimeOffset_ms = extractLong(line);
        } else if (line.find("\"ahead_behind_zero_offset_ms\"") != std::string::npos) {
            state.ahead_behind_zero_offset_ms = extractLong(line);
        } else if (line.find("\"total_distance_adjust_cm\"") != std::string::npos) {
            state.total_distance_adjust_cm = static_cast<long>(extractLong(line));
        } else if (line.find("\"trip_distance_adjust_cm\"") != std::string::npos) {
            state.trip_distance_adjust_cm = static_cast<long>(extractLong(line));
        } else if (line.find("\"auto_start_rally_time_s\"") != std::string::npos) {
            state.auto_start_rally_time_s = static_cast<uint64_t>(extractLong(line));
        } else if (line.find("\"auto_start_rally_time_minutes\"") != std::string::npos) {
            // Pre-seconds config: carry a pending autostart across the upgrade
            // rather than silently dropping it. Only honoured if the seconds
            // key has not already been read.
            if (state.auto_start_rally_time_s == 0) {
                state.auto_start_rally_time_s =
                    static_cast<uint64_t>(extractLong(line)) * 60;
            }
        } else if (line.find("\"driver_window_x\"") != std::string::npos) {
            state.driver_window_x = static_cast<int>(extractLong(line));
        } else if (line.find("\"driver_window_y\"") != std::string::npos) {
            state.driver_window_y = static_cast<int>(extractLong(line));
        } else if (line.find("\"driver_window_width\"") != std::string::npos) {
            state.driver_window_width = static_cast<int>(extractLong(line));
        } else if (line.find("\"driver_window_height\"") != std::string::npos) {
            state.driver_window_height = static_cast<int>(extractLong(line));
        } else if (line.find("\"driver_window_monitor\"") != std::string::npos) {
            state.driver_window_monitor = static_cast<int>(extractLong(line));
        } else if (line.find("\"alarm_distance_km\"") != std::string::npos) {
            state.alarm_distance_km = static_cast<int>(extractLong(line));
        } else if (line.find("\"alarm_target_counts\"") != std::string::npos) {
            state.alarm_target_counts = extractLong(line);
        } else if (line.find("\"force_single_display\"") != std::string::npos) {
            state.force_single_display = extractBool(line);
        } else if (line.find("\"tone_enabled\"") != std::string::npos) {
            state.tone_enabled = extractBool(line);
        } else if (line.find("\"simple_tone_mode\"") != std::string::npos) {
            state.simple_tone_mode = extractBool(line);
        } else if (line.find("\"web_enabled\"") != std::string::npos) {
            state.web_enabled = extractBool(line);
        } else if (line.find("\"web_port\"") != std::string::npos) {
            state.web_port = static_cast<int>(extractLong(line));
        } else if (line.find("\"auto_start_early_departure\"") != std::string::npos) {
            state.auto_start_early_departure = extractBool(line);
        } else if (line.find("\"beep_assist_enabled\"") != std::string::npos) {
            state.beep_assist_enabled = extractBool(line);
        } else if (line.find("\"beep_advance_m\"") != std::string::npos) {
            state.beep_advance_m = extractDouble(line);
        } else if (line.find("\"beep_advance_s\"") != std::string::npos) {
            state.beep_advance_s = extractDouble(line);
        } else if (line.find("\"beep_navigation_mode\"") != std::string::npos) {
            state.beep_navigation_mode = extractBool(line);
        } else if (line.find("\"beep_timing_mode\"") != std::string::npos) {
            state.beep_timing_mode = extractBool(line);
        } else if (line.find("\"memory_1_waypoints\"") != std::string::npos) {
            parseDoubleArray(stream, state.memory_slots[0].beep_waypoints_m);
        } else if (line.find("\"memory_2_waypoints\"") != std::string::npos) {
            parseDoubleArray(stream, state.memory_slots[1].beep_waypoints_m);
        } else if (line.find("\"memory_3_waypoints\"") != std::string::npos) {
            parseDoubleArray(stream, state.memory_slots[2].beep_waypoints_m);
        } else if (line.find("\"memory_4_waypoints\"") != std::string::npos) {
            parseDoubleArray(stream, state.memory_slots[3].beep_waypoints_m);
        } else if (line.find("\"memory_5_waypoints\"") != std::string::npos) {
            parseDoubleArray(stream, state.memory_slots[4].beep_waypoints_m);
        } else if (line.find("\"beep_waypoints_m\"") != std::string::npos) {
            parseDoubleArray(stream, state.beep_waypoints_m);
        }
    }
}

static void writeDoubleArray(std::ofstream& file, const std::string& name,
                             const std::vector<double>& values, bool trailing_comma) {
    file << "  \"" << name << "\": [\n";
    for (size_t i = 0; i < values.size(); i++) {
        file << "    " << values[i];
        if (i + 1 < values.size()) file << ",";
        file << "\n";
    }
    file << "  ]";
    if (trailing_comma) file << ",";
    file << "\n";
}

static void writeSegmentArray(std::ofstream& file, const std::string& name,
                              const std::vector<Segment>& segs, bool trailing_comma) {
    file << "  \"" << name << "\": [\n";
    for (size_t i = 0; i < segs.size(); i++) {
        file << "    {\n";
        file << std::fixed << std::setprecision(6);
        file << "      \"target_speed_kph\": " << segs[i].target_speed_kph << ",\n";
        file << "      \"target_speed_counts_per_hour\": " << segs[i].target_speed_counts_per_hour << ",\n";
        file << "      \"distance_m\": " << segs[i].distance_m << ",\n";
        file << "      \"distance_counts\": " << segs[i].distance_counts << ",\n";
        file << "      \"autoNext\": " << (segs[i].autoNext ? "true" : "false") << "\n";
        file << "    }";
        if (i < segs.size() - 1) file << ",";
        file << "\n";
    }
    file << "  ]";
    if (trailing_comma) file << ",";
    file << "\n";
}

void ConfigFile::save(const RallyState& state, const std::string& path) {
    std::ofstream file(path);
    if (!file.is_open()) {
        return;
    }
    
    file << "{\n";
    file << "  \"units\": " << (state.units ? "true" : "false") << ",\n";
    file << "  \"calibration\": " << state.calibration << ",\n";
    file << "  \"counters\": " << (state.counters ? "true" : "false") << ",\n";
    file << "  \"total_start_cntr1\": " << state.total_start_cntr1 << ",\n";
    file << "  \"total_start_cntr2\": " << state.total_start_cntr2 << ",\n";
    file << "  \"total_start_time_ms\": " << state.total_start_time_ms << ",\n";
    file << "  \"trip_start_cntr1\": " << state.trip_start_cntr1 << ",\n";
    file << "  \"trip_start_cntr2\": " << state.trip_start_cntr2 << ",\n";
    file << "  \"trip_start_time_ms\": " << state.trip_start_time_ms << ",\n";
    file << "  \"segment_start_cntr1\": " << state.segment_start_cntr1 << ",\n";
    file << "  \"segment_start_cntr2\": " << state.segment_start_cntr2 << ",\n";
    file << "  \"segment_start_time_ms\": " << state.segment_start_time_ms << ",\n";
    file << "  \"segment_current_number\": " << state.segment_current_number << ",\n";
    file << "  \"rallyTimeOffset_ms\": " << state.rallyTimeOffset_ms << ",\n";
    file << "  \"ahead_behind_zero_offset_ms\": " << state.ahead_behind_zero_offset_ms << ",\n";
    file << "  \"total_distance_adjust_cm\": " << state.total_distance_adjust_cm << ",\n";
    file << "  \"trip_distance_adjust_cm\": " << state.trip_distance_adjust_cm << ",\n";
    file << "  \"auto_start_rally_time_s\": " << state.auto_start_rally_time_s << ",\n";
    file << "  \"driver_window_x\": " << state.driver_window_x << ",\n";
    file << "  \"driver_window_y\": " << state.driver_window_y << ",\n";
    file << "  \"driver_window_width\": " << state.driver_window_width << ",\n";
    file << "  \"driver_window_height\": " << state.driver_window_height << ",\n";
    file << "  \"driver_window_monitor\": " << state.driver_window_monitor << ",\n";
    file << "  \"alarm_distance_km\": " << state.alarm_distance_km << ",\n";
    file << "  \"alarm_target_counts\": " << state.alarm_target_counts << ",\n";
    file << "  \"force_single_display\": " << (state.force_single_display ? "true" : "false") << ",\n";
    file << "  \"tone_enabled\": " << (state.tone_enabled ? "true" : "false") << ",\n";
    file << "  \"simple_tone_mode\": " << (state.simple_tone_mode ? "true" : "false") << ",\n";
    file << "  \"web_enabled\": " << (state.web_enabled ? "true" : "false") << ",\n";
    file << "  \"web_port\": " << state.web_port << ",\n";
    
    file << "  \"beep_assist_enabled\": " << (state.beep_assist_enabled ? "true" : "false") << ",\n";
    file << "  \"beep_advance_m\": " << state.beep_advance_m << ",\n";
    file << "  \"beep_advance_s\": " << state.beep_advance_s << ",\n";
    file << "  \"beep_navigation_mode\": " << (state.beep_navigation_mode ? "true" : "false") << ",\n";
    file << "  \"beep_timing_mode\": " << (state.beep_timing_mode ? "true" : "false") << ",\n";
    file << "  \"auto_start_early_departure\": "
         << (state.auto_start_early_departure ? "true" : "false") << ",\n";
    file << "  \"beep_waypoints_m\": [\n";
    for (size_t i = 0; i < state.beep_waypoints_m.size(); i++) {
        file << "    " << state.beep_waypoints_m[i];
        if (i + 1 < state.beep_waypoints_m.size()) file << ",";
        file << "\n";
    }
    file << "  ],\n";

    // Check if any memory slots are populated
    bool has_memory = false;
    for (int i = 0; i < RallyState::MAX_MEMORY_SLOTS; i++) {
        if (!state.memory_slots[i].empty()) { has_memory = true; break; }
    }

    writeSegmentArray(file, "segments", state.segments, has_memory);
    
    if (has_memory) {
        int last_populated = -1;
        for (int i = RallyState::MAX_MEMORY_SLOTS - 1; i >= 0; i--) {
            if (!state.memory_slots[i].empty()) { last_populated = i; break; }
        }
        for (int i = 0; i < RallyState::MAX_MEMORY_SLOTS; i++) {
            if (!state.memory_slots[i].empty()) {
                std::string name = "memory_" + std::to_string(i + 1);
                // Segments always take a trailing comma here: the slot's
                // waypoints array follows them and carries the real one.
                writeSegmentArray(file, name, state.memory_slots[i].segments, true);
                writeDoubleArray(file, name + "_waypoints",
                                 state.memory_slots[i].beep_waypoints_m, i < last_populated);
            }
        }
    }
    
    file << "}\n";
}
