#ifndef TEST_SEGMENT_ROW_H
#define TEST_SEGMENT_ROW_H

#include "test_framework.h"
#include "../calculations.h"
#include <string>

// Tests for the co-pilot's next-segment row labels. The heading shows the
// current segment's own target speed -- the one thing about it the app can
// state honestly -- not a fabricated segment/stage number.
class TestSegmentRow {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Segment Row Labels");

        suite->addTest("heading shows the current segment's target speed", []() {
            return segmentRowHeading(30.0, true) == "30 >"
                // Rounds the same way the speed cell does.
                && segmentRowHeading(47.6, true) == "48 >";
        });

        suite->addTest("heading falls back when no segment is running", []() {
            return segmentRowHeading(0.0, false) == "--->";
        });

        suite->addTest("speed cell shows only the coming change, no current-speed prefix, following the unit", []() {
            // The current speed is already the row's heading; repeating it
            // here would say the same number twice. Numbers are pre-converted
            // by the caller -- this only picks which unit word to print.
            return segmentSpeedTransition(40.0, true, false) == "> 40 kph"
                && segmentSpeedTransition(25.0, true, true) == "> 25 mph";
        });

        suite->addTest("speed cell marks the last segment as the end, regardless of unit", []() {
            return segmentSpeedTransition(0.0, false, false) == "> END"
                && segmentSpeedTransition(0.0, false, true) == "> END";
        });

        suite->addTest("speeds are shown as whole numbers", []() {
            // Roadbook speeds are called as whole figures; 52.4 reads as noise.
            return segmentSpeedTransition(52.4, true, false) == "> 52 kph";
        });

        return suite;
    }
};

#endif // TEST_SEGMENT_ROW_H
