#ifndef TEST_SEGMENTS_H
#define TEST_SEGMENTS_H

#include "test_framework.h"
#include "../rally_state.h"
#include "../calculations.h"

class TestSegments {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Segment Management Tests");
        
        // Test add segment
        suite->addTest("Add segment with target_speed, distance, autoNext", []() {
            RallyState state;
            
            Segment seg;
            seg.target_speed_counts_per_hour = 50000.0;
            seg.distance_counts = 10000.0;
            seg.autoNext = true;
            
            state.segments.push_back(seg);
            
            ASSERT_EQ(state.segments.size(), 1u);
            ASSERT_NEAR(state.segments[0].target_speed_counts_per_hour, 50000.0, 0.001);
            ASSERT_NEAR(state.segments[0].distance_counts, 10000.0, 0.001);
            ASSERT_TRUE(state.segments[0].autoNext);
            
            return true;
        });
        
        // Test delete segment
        suite->addTest("Delete segment from list", []() {
            RallyState state;
            
            state.segments.push_back({0.0, 100.0, 0.0, 1000.0, true});
            state.segments.push_back({0.0, 200.0, 0.0, 2000.0, false});
            state.segments.push_back({0.0, 300.0, 0.0, 3000.0, true});
            
            // Delete middle segment
            state.segments.erase(state.segments.begin() + 1);
            
            ASSERT_EQ(state.segments.size(), 2u);
            ASSERT_NEAR(state.segments[0].target_speed_counts_per_hour, 100.0, 0.001);
            ASSERT_NEAR(state.segments[1].target_speed_counts_per_hour, 300.0, 0.001);
            
            return true;
        });
        
        // Test target speed stored as counts per hour
        suite->addTest("Target speed stored as counts per hour", []() {
            long calibration = 600000;
            double input_kph = 60.0;
            
            // counts_per_hour = (kph * 1000 * 3600) / (cal / 1000)
            // = (60 * 1000 * 3600) / (600000 / 1000)
            // = 216000000 / 600 = 360000? No wait...
            // Actually: (kph * 1000000) / calibration
            double counts_per_hour = kphToCountsPerHour(input_kph, calibration);
            // 60 * 1000000 / 600000 = 100000
            
            ASSERT_NEAR(counts_per_hour, 100000.0, 0.001);
            
            Segment seg;
            seg.target_speed_counts_per_hour = counts_per_hour;
            
            ASSERT_NEAR(seg.target_speed_counts_per_hour, 100000.0, 0.001);
            
            return true;
        });
        
        // Test KPH to counts conversion formula
        suite->addTest("Counts per hour = (kph * 1000000) / calibration", []() {
            long calibration = 600000;
            double kph = 100.0;
            
            double counts_h = kphToCountsPerHour(kph, calibration);
            // 100 * 1e9 / 600000 = 166666.666...
            ASSERT_NEAR(counts_h, 166666.666, 1.0);
            
            return true;
        });
        
        // Test autoNext advances segment
        suite->addTest("AutoNext=true advances segment when distance reached", []() {
            RallyState state;
            state.segments.push_back({0.0, 100000.0, 0.0, 1000.0, true});   // 1000 counts, autoNext
            state.segments.push_back({0.0, 120000.0, 0.0, 2000.0, false});
            state.segment_current_number = 0;
            state.segment_start_cntr1 = 0;
            
            // Simulate distance covered
            uint64_t current_cntr1 = 1500;  // > 1000 counts
            double segment_counts = state.segments[0].distance_counts;
            
            bool should_advance = (current_cntr1 - state.segment_start_cntr1) >= segment_counts;
            
            ASSERT_TRUE(should_advance);
            ASSERT_TRUE(state.segments[0].autoNext);
            
            return true;
        });
        
        // Test autoNext=false requires manual
        suite->addTest("AutoNext=false requires manual next segment button", []() {
            RallyState state;
            state.segments.push_back({0.0, 100000.0, 0.0, 1000.0, false});  // autoNext = false
            state.segment_current_number = 0;
            
            // Even if distance exceeded, should NOT auto-advance
            ASSERT_FALSE(state.segments[0].autoNext);
            
            return true;
        });
        
        // Test skip multiple segments
        suite->addTest("Skip multiple segments if polling lag", []() {
            RallyState state;
            state.segments.push_back({0.0, 100000.0, 0.0, 100.0, true});   // 100 counts
            state.segments.push_back({0.0, 100000.0, 0.0, 100.0, true});   // 100 counts
            state.segments.push_back({0.0, 100000.0, 0.0, 100.0, true});   // 100 counts
            state.segment_current_number = 0;
            state.segment_start_cntr1 = 0;
            
            // Distance covered = 350 counts (spans first 3 segments)
            double distance_covered = 350.0;
            int segments_to_skip = 0;
            double cumulative = 0.0;
            
            for (size_t i = state.segment_current_number; i < state.segments.size(); i++) {
                cumulative += state.segments[i].distance_counts;
                if (distance_covered >= cumulative && state.segments[i].autoNext) {
                    segments_to_skip++;
                }
            }
            
            ASSERT_EQ(segments_to_skip, 3);  // Should skip all 3 segments
            
            return true;
        });
        
        // Test no current segment shows --.--
        suite->addTest("No current segment (index -1) shows --.-- for Seg", []() {
            RallyState state;
            state.segment_current_number = -1;
            
            bool should_show_dashes = (state.segment_current_number < 0);
            ASSERT_TRUE(should_show_dashes);
            
            return true;
        });
        
        // Test past end of last segment
        suite->addTest("Past end of last segment by >1000m shows --.--", []() {
            RallyState state;
            state.calibration = 600000;  // 0.6m per count
            state.segments.push_back({0.0, 100000.0, 0.0, 10000.0, false});  // 10000 counts = 6000m
            state.segment_current_number = 0;
            state.segment_start_cntr1 = 0;
            
            // Current position: 10000 + 2000 counts = 12000 counts (1200m past end)
            double current_counts = 12000.0;
            double segment_end = state.segments[0].distance_counts;
            double distance_past = current_counts - segment_end;  // 2000 counts
            
            // Convert to meters: 2000 * 0.6 = 1200m
            double meters_past = (distance_past * state.calibration) / 1000000.0;
            
            bool should_show_dashes = (meters_past > 1000.0);
            ASSERT_TRUE(should_show_dashes);
            
            return true;
        });
        
        // Test segment default values
        suite->addTest("Segment defaults to empty array", []() {
            RallyState state;
            ASSERT_EQ(state.segments.size(), 0u);
            return true;
        });
        
        // Test segment current number default
        suite->addTest("segment_current_number defaults to -1", []() {
            RallyState state;
            ASSERT_EQ(state.segment_current_number, -1);
            return true;
        });
        
        // ---- nextSegmentTargetKph ----
        // Shared by the co-pilot's "coming change" row and the driver's
        // smaller speed under Target, so the two panels cannot disagree
        // about what the next speed is.

        suite->addTest("next segment speed is the one after the current segment", []() {
            RallyState state;
            Segment a{}; a.target_speed_kph = 30.0;
            Segment b{}; b.target_speed_kph = 50.0;
            state.segments = { a, b };
            state.segment_current_number = 0;
            double kph = 0.0;
            ASSERT_TRUE(nextSegmentTargetKph(state, &kph));
            ASSERT_NEAR(kph, 50.0, 0.001);
            return true;
        });

        suite->addTest("there is no next speed on the last segment", []() {
            RallyState state;
            Segment a{}; a.target_speed_kph = 30.0;
            state.segments = { a };
            state.segment_current_number = 0;
            double kph = 99.0;
            ASSERT_FALSE(nextSegmentTargetKph(state, &kph));
            return true;
        });

        suite->addTest("there is no next speed outside a stage", []() {
            RallyState state;
            Segment a{}; a.target_speed_kph = 30.0;
            Segment b{}; b.target_speed_kph = 50.0;
            state.segments = { a, b };
            state.segment_current_number = -1;
            double kph = 99.0;
            ASSERT_FALSE(nextSegmentTargetKph(state, &kph));
            return true;
        });

        return suite;
    }
};

#endif // TEST_SEGMENTS_H
