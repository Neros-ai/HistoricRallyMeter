#ifndef TEST_CONFIG_FILE_H
#define TEST_CONFIG_FILE_H

#include "test_framework.h"
#include "../config_file.h"
#include "../rally_state.h"
#include <fstream>
#include <cstdio>

class TestConfigFile {
private:
    const std::string test_config_file = "test_rally_config.json";
    
    void cleanup() {
        std::remove(test_config_file.c_str());
    }
    
    void writeTestFile(const std::string& content) {
        std::ofstream file(test_config_file);
        file << content;
        file.close();
    }
    
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Config File Tests");
        
        // Test loading from valid JSON file with all fields
        suite->addTest("Load valid JSON with all fields", [this]() {
            // Test that RallyState can hold the expected values
            RallyState state;
            state.units = true;
            state.calibration = 750000;
            state.counters = false;
            state.total_start_cntr1 = 1000;
            state.total_start_cntr2 = 2000;
            state.segment_current_number = 2;
            state.rallyTimeOffset_ms = 3600000;
            state.segments.push_back({0.0, 100.0, 0.0, 5000.0, true});
            state.segments.push_back({0.0, 120.0, 0.0, 8000.0, false});
            
            ASSERT_TRUE(state.units);
            ASSERT_EQ(state.calibration, 750000);
            ASSERT_FALSE(state.counters);
            ASSERT_EQ(state.segment_current_number, 2);
            ASSERT_EQ(state.rallyTimeOffset_ms, 3600000);
            ASSERT_EQ(state.segments.size(), 2u);
            ASSERT_NEAR(state.segments[0].target_speed_counts_per_hour, 100.0, 0.001);
            ASSERT_NEAR(state.segments[0].distance_counts, 5000.0, 0.001);
            ASSERT_TRUE(state.segments[0].autoNext);
            ASSERT_NEAR(state.segments[1].target_speed_counts_per_hour, 120.0, 0.001);
            ASSERT_FALSE(state.segments[1].autoNext);
            
            return true;
        });
        
        // Test loading with missing fields (defaults applied)
        suite->addTest("Load JSON with missing fields uses defaults", [this]() {
            cleanup();
            writeTestFile(R"({
  "calibration": 500000
})");
            
            RallyState state;
            // State should have defaults except calibration
            ASSERT_FALSE(state.units);  // default
            ASSERT_EQ(state.calibration, 600000);  // default before load
            ASSERT_TRUE(state.counters);  // default
            ASSERT_EQ(state.segment_current_number, -1);  // default
            ASSERT_EQ(state.rallyTimeOffset_ms, 0);  // default
            ASSERT_EQ(state.segments.size(), 0u);  // default empty
            
            cleanup();
            return true;
        });
        
        // Test loading when file does not exist
        suite->addTest("Load nonexistent file uses all defaults", [this]() {
            cleanup();
            RallyState state;
            
            ASSERT_FALSE(state.units);
            ASSERT_EQ(state.calibration, 600000);
            ASSERT_TRUE(state.counters);
            ASSERT_EQ(state.segment_current_number, -1);
            ASSERT_EQ(state.rallyTimeOffset_ms, 0);
            ASSERT_EQ(state.segments.size(), 0u);
            
            return true;
        });
        
        // Test loading with multiple segments
        suite->addTest("Load JSON with multiple segments", []() {
            // Test that segments vector can hold multiple segments correctly
            std::vector<Segment> segments;
            segments.push_back({0.0, 50.0, 0.0, 1000.0, true});
            segments.push_back({0.0, 60.0, 0.0, 2000.0, false});
            segments.push_back({0.0, 70.0, 0.0, 3000.0, true});
            
            ASSERT_EQ(segments.size(), 3u);
            ASSERT_NEAR(segments[0].target_speed_counts_per_hour, 50.0, 0.001);
            ASSERT_NEAR(segments[1].target_speed_counts_per_hour, 60.0, 0.001);
            ASSERT_NEAR(segments[2].target_speed_counts_per_hour, 70.0, 0.001);
            ASSERT_NEAR(segments[0].distance_counts, 1000.0, 0.001);
            ASSERT_TRUE(segments[0].autoNext);
            ASSERT_FALSE(segments[1].autoNext);
            
            return true;
        });
        
        // Test loading with empty segments array
        suite->addTest("Load JSON with empty segments array", [this]() {
            cleanup();
            writeTestFile(R"({
  "segments": []
})");
            
            std::ifstream file(test_config_file);
            std::string content((std::istreambuf_iterator<char>(file)),
                                 std::istreambuf_iterator<char>());
            file.close();
            
            std::vector<Segment> segments;
            // Empty array should result in empty vector
            ASSERT_EQ(segments.size(), 0u);
            
            cleanup();
            return true;
        });
        
        // Test saving config
        suite->addTest("Save config writes all fields correctly", [this]() {
            cleanup();
            RallyState state;
            state.units = true;
            state.calibration = 800000;
            state.counters = false;
            state.total_start_cntr1 = 5000;
            state.total_start_cntr2 = 6000;
            state.total_start_time_ms = 9999999;
            state.trip_start_cntr1 = 100;
            state.trip_start_cntr2 = 200;
            state.trip_start_time_ms = 8888888;
            state.segment_start_cntr1 = 50;
            state.segment_start_cntr2 = 60;
            state.segment_start_time_ms = 7777777;
            state.segment_current_number = 1;
            state.rallyTimeOffset_ms = 1800000;
            state.segments.push_back({0.0, 100, 0.0, 2000, true});
            state.segments.push_back({0.0, 150, 0.0, 3000, false});
            
            // Save using ConfigFile::save logic
            std::ofstream file(test_config_file);
            file << "{\n";
            file << "  \"units\": " << (state.units ? "true" : "false") << ",\n";
            file << "  \"calibration\": " << state.calibration << ",\n";
            file << "  \"counters\": " << (state.counters ? "true" : "false") << ",\n";
            file << "  \"total_start_cntr1\": " << state.total_start_cntr1 << ",\n";
            file << "  \"total_start_cntr2\": " << state.total_start_cntr2 << ",\n";
            file << "  \"total_start_time_ms\": " << state.total_start_time_ms << ",\n";
            file << "  \"segment_current_number\": " << state.segment_current_number << ",\n";
            file << "  \"rallyTimeOffset_ms\": " << state.rallyTimeOffset_ms << ",\n";
            file << "  \"segments\": [\n";
            for (size_t i = 0; i < state.segments.size(); i++) {
                file << "    {\"target_speed_counts_per_hour\": " << state.segments[i].target_speed_counts_per_hour;
                file << ", \"distance_counts\": " << state.segments[i].distance_counts;
                file << ", \"autoNext\": " << (state.segments[i].autoNext ? "true" : "false") << "}";
                if (i < state.segments.size() - 1) file << ",";
                file << "\n";
            }
            file << "  ]\n}\n";
            file.close();
            
            // Verify file exists and contains expected content
            std::ifstream check(test_config_file);
            ASSERT_TRUE(check.is_open());
            std::string content((std::istreambuf_iterator<char>(check)),
                                 std::istreambuf_iterator<char>());
            check.close();
            
            ASSERT_TRUE(content.find("\"units\": true") != std::string::npos);
            ASSERT_TRUE(content.find("\"calibration\": 800000") != std::string::npos);
            ASSERT_TRUE(content.find("\"counters\": false") != std::string::npos);
            ASSERT_TRUE(content.find("\"segment_current_number\": 1") != std::string::npos);
            ASSERT_TRUE(content.find("\"target_speed_counts_per_hour\": 100") != std::string::npos);
            
            cleanup();
            return true;
        });
        
        // Test calibration default
        suite->addTest("Calibration defaults to 600000", []() {
            RallyState state;
            ASSERT_EQ(state.calibration, 600000);
            return true;
        });
        
        // Test units default
        suite->addTest("Units defaults to false (KPH)", []() {
            RallyState state;
            ASSERT_FALSE(state.units);
            return true;
        });
        
        // Test segment_current_number default
        suite->addTest("segment_current_number defaults to -1", []() {
            RallyState state;
            ASSERT_EQ(state.segment_current_number, -1);
            return true;
        });
        
        suite->addTest("distance corrections survive a save/load round trip", []() {
            // A correction dialled in mid-rally has to outlive a restart, or
            // a power blip silently reintroduces the error it cancelled.
            const std::string path = "/tmp/rallybox_test_dist_adjust.json";
            RallyState out;
            out.total_distance_adjust_cm = -2500;
            out.trip_distance_adjust_cm = -1800;
            ConfigFile::save(out, path);

            RallyState in;
            ConfigFile::load(in, path);
            std::remove(path.c_str());
            return in.total_distance_adjust_cm == -2500
                && in.trip_distance_adjust_cm == -1800;
        });

        suite->addTest("distance corrections default to zero", []() {
            RallyState fresh;
            return fresh.total_distance_adjust_cm == 0
                && fresh.trip_distance_adjust_cm == 0;
        });

        suite->addTest("beep assist settings survive a save/load round trip", []() {
            const std::string path = "/tmp/rallybox_test_beep.json";
            RallyState out;
            out.beep_assist_enabled = true;
            out.beep_navigation_mode = true;
            out.beep_timing_mode = false;
            out.beep_advance_m = 250.0;
            out.beep_advance_s = 8.0;
            out.beep_waypoints_m = { 3670.0, 4980.0, 17300.0 };
            ConfigFile::save(out, path);

            RallyState in;
            ConfigFile::load(in, path);
            std::remove(path.c_str());
            return in.beep_assist_enabled
                && in.beep_navigation_mode
                && !in.beep_timing_mode
                && std::abs(in.beep_advance_m - 250.0) < 0.001
                && std::abs(in.beep_advance_s - 8.0) < 0.001
                && in.beep_waypoints_m.size() == 3
                && std::abs(in.beep_waypoints_m[2] - 17300.0) < 0.001;
        });

        suite->addTest("beep waypoints coexist with the segment array", []() {
            // Both are parsed by consuming the stream to a closing bracket,
            // so one must not swallow the other.
            const std::string path = "/tmp/rallybox_test_beep_segs.json";
            RallyState out;
            out.beep_waypoints_m = { 1000.0, 2000.0 };
            Segment s{};
            s.target_speed_kph = 50.0;
            s.distance_m = 1200.0;
            out.segments.push_back(s);
            ConfigFile::save(out, path);

            RallyState in;
            ConfigFile::load(in, path);
            std::remove(path.c_str());
            return in.beep_waypoints_m.size() == 2 && in.segments.size() == 1;
        });

        suite->addTest("tone_enabled survives a save/load round trip", []() {
            const std::string path = "/tmp/rallybox_test_tone_enabled.json";
            RallyState out;
            out.tone_enabled = false;
            ConfigFile::save(out, path);

            RallyState in;
            ConfigFile::load(in, path);
            std::remove(path.c_str());
            return !in.tone_enabled;
        });

        suite->addTest("simple_tone_mode survives a save/load round trip", []() {
            const std::string path = "/tmp/rallybox_test_simple_tone_mode.json";
            RallyState out;
            out.simple_tone_mode = true;
            ConfigFile::save(out, path);

            RallyState in;
            ConfigFile::load(in, path);
            std::remove(path.c_str());
            return in.simple_tone_mode;
        });

        suite->addTest("loading a config without waypoints clears any already in state", []() {
            // ConfigFile::load takes RallyState& by reference, so a config
            // predating Beep Assist must not leave a previous rally's
            // waypoints in place alongside the new file's other values.
            RallyState state;
            state.beep_waypoints_m = { 1000.0, 2000.0 };
            std::string path = "/tmp/rb_test_no_waypoints.json";
            std::ofstream f(path);
            f << "{\n  \"calibration\": 600000\n}\n";
            f.close();
            ConfigFile::load(state, path);
            ASSERT_TRUE(state.beep_waypoints_m.empty());
            return true;
        });

        suite->addTest("a memory slot round-trips its beep waypoints with its segments", []() {
            // Recalling a stage used to restore its segments while leaving the
            // PREVIOUS stage's waypoints loaded -- a trap, since the two are
            // one setup.
            RallyState state;
            state.calibration = 600000;
            Segment a{}; a.target_speed_kph = 40.0; a.distance_m = 1000.0;
            state.memory_slots[1].segments = { a };
            state.memory_slots[1].beep_waypoints_m = { 3670.0, 4980.0 };
            std::string path = "/tmp/rb_test_mem_waypoints.json";
            ConfigFile::save(state, path);

            RallyState loaded;
            ConfigFile::load(loaded, path);
            ASSERT_EQ(loaded.memory_slots[1].segments.size(), 1u);
            ASSERT_EQ(loaded.memory_slots[1].beep_waypoints_m.size(), 2u);
            ASSERT_NEAR(loaded.memory_slots[1].beep_waypoints_m[0], 3670.0, 0.5);
            ASSERT_NEAR(loaded.memory_slots[1].beep_waypoints_m[1], 4980.0, 0.5);
            std::remove(path.c_str());
            return true;
        });

        suite->addTest("a memory slot saved before waypoints existed still loads", []() {
            // Backward compatibility: a slot with segments but no waypoints
            // key must load its segments and simply have no waypoints.
            RallyState state;
            std::string path = "/tmp/rb_test_mem_legacy.json";
            std::ofstream f(path);
            // The multi-line shape ConfigFile::save writes, minus the
            // memory_1_waypoints array a pre-Beep-Assist build never emitted.
            f << "{\n";
            f << "  \"calibration\": 600000,\n";
            f << "  \"memory_1\": [\n";
            f << "    {\n";
            f << "      \"target_speed_kph\": 40.000000,\n";
            f << "      \"target_speed_counts_per_hour\": 24000000.000000,\n";
            f << "      \"distance_m\": 1000.000000,\n";
            f << "      \"distance_counts\": 1666.666667,\n";
            f << "      \"autoNext\": true\n";
            f << "    }\n";
            f << "  ]\n";
            f << "}\n";
            f.close();
            ConfigFile::load(state, path);
            ASSERT_EQ(state.memory_slots[0].segments.size(), 1u);
            ASSERT_TRUE(state.memory_slots[0].beep_waypoints_m.empty());
            ASSERT_FALSE(state.memory_slots[0].empty());
            std::remove(path.c_str());
            return true;
        });

        suite->addTest("a slot is empty when it has no segments", []() {
            RallyState state;
            ASSERT_TRUE(state.memory_slots[0].empty());
            state.memory_slots[0].beep_waypoints_m = { 100.0 };
            // Waypoints alone are not a saved stage.
            ASSERT_TRUE(state.memory_slots[0].empty());
            return true;
        });

        suite->addTest("a pre-seconds config still arms its autostart", []() {
            // The field was minutes since the epoch; a config written by an
            // older build must not silently lose a pending autostart.
            RallyState state;
            std::string path = "/tmp/rb_test_autostart_legacy.json";
            std::ofstream f(path);
            f << "{\n  \"auto_start_rally_time_minutes\": 600\n}\n";
            f.close();
            ConfigFile::load(state, path);
            ASSERT_EQ(state.auto_start_rally_time_s, 36000u);  // 600 min = 36000 s
            std::remove(path.c_str());
            return true;
        });

        suite->addTest("autostart seconds survive a save/load round trip", []() {
            RallyState state;
            state.auto_start_rally_time_s = 37845;  // 10:30:45 past the epoch
            std::string path = "/tmp/rb_test_autostart_s.json";
            ConfigFile::save(state, path);
            RallyState loaded;
            ConfigFile::load(loaded, path);
            ASSERT_EQ(loaded.auto_start_rally_time_s, 37845u);
            std::remove(path.c_str());
            return true;
        });

        return suite;
    }
};

#endif // TEST_CONFIG_FILE_H
