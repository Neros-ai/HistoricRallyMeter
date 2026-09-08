#ifndef TEST_SEGMENTS_H
#define TEST_SEGMENTS_H

#include "test_framework.h"
#include <algorithm>
#include <cmath>
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
            state.stage_segments = { a, b };
            state.segment_current_number = 0;
            double kph = 0.0;
            ASSERT_TRUE(nextSegmentTargetKph(state, &kph));
            ASSERT_NEAR(kph, 50.0, 0.001);
            return true;
        });

        suite->addTest("there is no next speed on the last segment", []() {
            RallyState state;
            Segment a{}; a.target_speed_kph = 30.0;
            state.stage_segments = { a };
            state.segment_current_number = 0;
            double kph = 99.0;
            ASSERT_FALSE(nextSegmentTargetKph(state, &kph));
            return true;
        });

        suite->addTest("there is no next speed outside a stage", []() {
            RallyState state;
            Segment a{}; a.target_speed_kph = 30.0;
            Segment b{}; b.target_speed_kph = 50.0;
            state.stage_segments = { a, b };
            state.segment_current_number = -1;
            double kph = 99.0;
            ASSERT_FALSE(nextSegmentTargetKph(state, &kph));
            return true;
        });

        suite->addTest("the status panel carries speed, segment length and cumulative", []() {
            Segment a{}; a.target_speed_kph = 40.0; a.distance_m = 148.0; a.autoNext = true;
            std::string panel = formatStageStatusTable({ a }, false, "none", 6);
            ASSERT_TRUE(panel.find("KPH") != std::string::npos);
            ASSERT_TRUE(panel.find("Distance (m)") != std::string::npos);
            // One heading over both distance columns, not one each.
            ASSERT_TRUE(panel.find("Cum") == std::string::npos);
            // The whole heading row is one coloured run.
            ASSERT_TRUE(panel.find(std::string("<span foreground=\"")
                                   + STAGE_STATUS_CAPTION_COLOR + "\">   KPH")
                        != std::string::npos);
            ASSERT_TRUE(panel.find("40.00") != std::string::npos);
            // No Auto or Time columns. ("Autostart" on the line above also
            // begins with "Auto", so check the heading itself.)
            ASSERT_TRUE(panel.find("(m)  Auto") == std::string::npos);
            ASSERT_TRUE(panel.find("Time") == std::string::npos);
            // One line per segment, never wrapped into a second column.
            ASSERT_EQ(std::count(panel.begin(), panel.end(), '\n'), 2);
            return true;
        });

        suite->addTest("the cumulative column runs from the stage start", []() {
            // The roadbook gives each segment's own length; the box counts
            // from the stage start, so the crew need the running total to
            // check the odometer against.
            std::vector<Segment> segs;
            for (double d : {148.0, 500.0, 500.0}) {
                Segment s{}; s.target_speed_kph = 40.0; s.distance_m = d;
                segs.push_back(s);
            }
            std::string panel = formatStageStatusTable(segs, false, "none", 6);
            ASSERT_TRUE(panel.find("148") != std::string::npos);
            ASSERT_TRUE(panel.find("648") != std::string::npos);
            ASSERT_TRUE(panel.find("1148") != std::string::npos);
            return true;
        });

        suite->addTest("the panel leads with the autostart and nothing else", []() {
            // "Stage: N" ahead of it was the widest field on the line, and
            // the panel sets the width of the grid column it sits in, so it
            // pushed the rest of the row off to the right.
            Segment a{}; a.target_speed_kph = 45.0; a.distance_m = 500.0;
            std::string panel = formatStageStatusTable({ a }, false, "none", 6);
            ASSERT_TRUE(panel.find("Stage") == std::string::npos);
            // Captions are coloured markup, values are not.
            ASSERT_TRUE(panel.find("Autostart:</span> none") != std::string::npos);

            std::string empty = formatStageStatusTable({}, false, "none", 6);
            ASSERT_TRUE(empty.find("(no segments set)") != std::string::npos);
            // No column headings when there is nothing to head.
            ASSERT_TRUE(empty.find("Speed") == std::string::npos);
            return true;
        });

        suite->addTest("the status panel converts speed with the display units", []() {
            Segment a{}; a.target_speed_kph = 100.0; a.distance_m = 1000.0;
            std::string mph = formatStageStatusTable({ a }, true, "none", 6);
            ASSERT_TRUE(mph.find("MPH") != std::string::npos);
            ASSERT_TRUE(mph.find("62.14") != std::string::npos);
            ASSERT_TRUE(mph.find("KPH") == std::string::npos);
            return true;
        });

        suite->addTest("one segment over the limit is shown, not summarised", []() {
            // "+1 more" costs exactly the row it replaces, so it earns
            // nothing; the summary starts at two.
            std::vector<Segment> segs;
            for (int i = 0; i < 7; i++) {
                Segment s{}; s.target_speed_kph = 40.0; s.distance_m = 100.0;
                segs.push_back(s);
            }
            std::string panel = formatStageStatusTable(segs, false, "none", 6);
            ASSERT_TRUE(panel.find("more") == std::string::npos);
            // Autostart line, heading, seven rows.
            ASSERT_EQ(std::count(panel.begin(), panel.end(), '\n'), 8);

            // Two over, and the summary is worth its line again.
            Segment extra{}; extra.target_speed_kph = 40.0; extra.distance_m = 100.0;
            segs.push_back(extra);
            std::string capped = formatStageStatusTable(segs, false, "none", 6);
            ASSERT_TRUE(capped.find("+2 more") != std::string::npos);
            return true;
        });

        suite->addTest("a stage taller than the panel is summarised", []() {
            std::vector<Segment> segs;
            for (int i = 0; i < 9; i++) {
                Segment s{}; s.target_speed_kph = 40.0; s.distance_m = 100.0;
                segs.push_back(s);
            }
            std::string panel = formatStageStatusTable(segs, false, "none", 6);
            ASSERT_TRUE(panel.find("+3 more") != std::string::npos);
            // Stage line, headings, six rows, summary.
            ASSERT_EQ(std::count(panel.begin(), panel.end(), '\n'), 8);
            return true;
        });

        suite->addTest("the autostart status is the fire time alone", []() {
            int64_t epoch_ms = 1000000000000LL;
            ASSERT_TRUE(formatAutoStartStatus(0, false, epoch_ms) == "none");
            // Both kinds read as just the time. The driver panel's countdown
            // is where the early-departure flag belongs.
            std::string early = formatAutoStartStatus(45, true, epoch_ms);
            std::string ordinary = formatAutoStartStatus(45, false, epoch_ms);
            ASSERT_TRUE(early.find("early") == std::string::npos);
            ASSERT_TRUE(early == ordinary);
            ASSERT_EQ(early.size(), 8u);
            return true;
        });

        suite->addTest("the average speed is held at zero while the autostart holds", []() {
            // The distance baselines are zeroed at arming but the clock keeps
            // running, so an unheld average reads as road speed the moment
            // the car creeps toward the line.
            ASSERT_NEAR(averageSpeedForDisplay(42.5, true), 0.0, 0.0001);
            ASSERT_NEAR(averageSpeedForDisplay(42.5, false), 42.5, 0.0001);
            return true;
        });

        suite->addTest("a stage is complete once its own distance is driven out", []() {
            Segment a{}; a.distance_counts = 1000.0;
            Segment b{}; b.distance_counts = 2000.0;
            std::vector<Segment> stage = { a, b };
            ASSERT_FALSE(stageDistanceComplete(stage, 0));
            ASSERT_FALSE(stageDistanceComplete(stage, 2999));
            // Exactly at the line counts as complete.
            ASSERT_TRUE(stageDistanceComplete(stage, 3000));
            ASSERT_TRUE(stageDistanceComplete(stage, 5000));
            return true;
        });

        suite->addTest("a stage with no segments is complete before it begins", []() {
            // Nothing to drive out, so nothing to protect: an edit is adopted
            // straight away rather than waiting for a distance that will
            // never be covered.
            ASSERT_TRUE(stageDistanceComplete({}, 0));
            return true;
        });

        suite->addTest("editing the roadbook does not touch the running stage", []() {
            // The stage is judged against the snapshot taken when it started.
            // Editing segments, or recalling a memory slot over them, is
            // preparation for the NEXT stage and must leave this one alone.
            RallyState state;
            Segment slow{}; slow.target_speed_kph = 30.0;
            Segment fast{}; fast.target_speed_kph = 50.0;
            state.stage_segments = { slow, fast };
            state.segments = { slow, fast };
            state.segment_current_number = 0;

            // The crew load a completely different roadbook for the next stage.
            Segment other{}; other.target_speed_kph = 90.0;
            state.segments = { other };

            double kph = 0.0;
            ASSERT_TRUE(nextSegmentTargetKph(state, &kph));
            ASSERT_NEAR(kph, 50.0, 0.001);
            return true;
        });

        suite->addTest("\"next\" moves one boundary and leaves the rest alone", []() {
            // The crew press "next" when the distance at which the speed
            // changes was not known in advance. The speed changes here; the
            // NEXT known point is unchanged in its distance from the stage
            // start, so the next segment simply becomes longer.
            std::vector<Segment> segs(2);
            segs[0].distance_counts = 1000.0;   // boundary 1 at 1000
            segs[1].distance_counts = 2000.0;   // boundary 2 at 3000
            ASSERT_TRUE(retimeSegmentBoundaryForward(segs, 0, 950, 1000));
            ASSERT_TRUE(std::abs(segs[0].distance_counts - 950.0) < 1e-6);
            ASSERT_TRUE(std::abs(segs[1].distance_counts - 2050.0) < 1e-6);
            // The point that matters: boundary 2, and so the stage total, is
            // exactly where the roadbook put it.
            ASSERT_TRUE(std::abs((segs[0].distance_counts + segs[1].distance_counts)
                                 - 3000.0) < 1e-6);
            return true;
        });

        suite->addTest("\"prev\" is the mirror, and also holds the later boundary", []() {
            std::vector<Segment> segs(2);
            segs[0].distance_counts = 1000.0;
            segs[1].distance_counts = 2000.0;
            // 200 into segment 2, the crew decide the change really came here.
            ASSERT_TRUE(retimeSegmentBoundaryBackward(segs, 1, 200, 1000));
            ASSERT_TRUE(std::abs(segs[0].distance_counts - 1200.0) < 1e-6);
            ASSERT_TRUE(std::abs(segs[1].distance_counts - 1800.0) < 1e-6);
            ASSERT_TRUE(std::abs((segs[0].distance_counts + segs[1].distance_counts)
                                 - 3000.0) < 1e-6);
            return true;
        });

        suite->addTest("retiming refuses when there is no neighbour to move it to", []() {
            std::vector<Segment> segs(1);
            segs[0].distance_counts = 1000.0;
            // Last segment: "next" has nowhere to hand the distance, and the
            // first segment has no predecessor to give it to. Changing one
            // side alone would silently resize the stage.
            ASSERT_FALSE(retimeSegmentBoundaryForward(segs, 0, 950, 1000));
            ASSERT_FALSE(retimeSegmentBoundaryBackward(segs, 0, 200, 1000));
            ASSERT_TRUE(std::abs(segs[0].distance_counts - 1000.0) < 1e-6);
            return true;
        });

        suite->addTest("a neighbour is never left shorter than nothing", []() {
            std::vector<Segment> segs(2);
            segs[0].distance_counts = 1000.0;
            segs[1].distance_counts = 100.0;
            // "next" pressed well past the segment end hands back a negative
            // distance; the next segment floors at zero rather than inverting.
            ASSERT_TRUE(retimeSegmentBoundaryForward(segs, 0, 2000, 1000));
            ASSERT_TRUE(segs[1].distance_counts >= 0.0);
            return true;
        });

        return suite;
    }
};

#endif // TEST_SEGMENTS_H
