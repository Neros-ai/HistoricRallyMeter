#ifndef TEST_STAGE_SUMMARY_H
#define TEST_STAGE_SUMMARY_H

#include "test_framework.h"
#include "../calculations.h"
#include <string>
#include <vector>

// Tests for the Stage Go confirmation's stage summary. Stage Go resets
// Total, Trip and the segment counters, so the operator has to see what they
// are about to start -- and the only truthful answer is the segments page's
// own content, not a name or number the app never actually tracks.
class TestStageSummary {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Stage Summary");

        suite->addTest("formats a single segment as distance@speed", []() {
            std::vector<Segment> segs;
            Segment s{}; s.distance_m = 500.0; s.target_speed_kph = 50.0;
            segs.push_back(s);
            return stageSummary(segs) == "500m@50.00kph";
        });

        suite->addTest("joins multiple segments in order with a comma", []() {
            std::vector<Segment> segs;
            Segment a{}; a.distance_m = 500.0; a.target_speed_kph = 50.0;
            Segment b{}; b.distance_m = 1200.0; b.target_speed_kph = 40.0;
            segs.push_back(a); segs.push_back(b);
            return stageSummary(segs) == "500m@50.00kph, 1200m@40.00kph";
        });

        suite->addTest("reports no stage when there are no segments", []() {
            std::vector<Segment> segs;
            return stageSummary(segs) == "No stage";
        });

        suite->addTest("truncates a fractional distance, matching the segments list", []() {
            // refreshSegmentList() displays distance via
            // static_cast<long>(distance_m) -- truncation, not rounding.
            // The dialog must show the identical number.
            std::vector<Segment> segs;
            Segment s{}; s.distance_m = 500.9; s.target_speed_kph = 50.0;
            segs.push_back(s);
            return stageSummary(segs) == "500m@50.00kph";
        });

        return suite;
    }
};

#endif // TEST_STAGE_SUMMARY_H
