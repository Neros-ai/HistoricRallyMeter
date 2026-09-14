#ifndef TEST_NEXT_PREV_H
#define TEST_NEXT_PREV_H

#include "test_framework.h"
#include "../calculations.h"
#include "../rally_state.h"

// next / prev at any point in a segment, and prev as an undo -- usership
// contingency 3 in docs/HANDOFF.md.
class TestNextPrev {
    static std::vector<Segment> roadbook() {
        // 1000 / 2000 / 1500, at 1 count per mm-ish: counts are what matter.
        std::vector<Segment> segs(3);
        segs[0].distance_counts = 1000; segs[0].target_speed_kph = 30;
        segs[1].distance_counts = 2000; segs[1].target_speed_kph = 45;
        segs[2].distance_counts = 1500; segs[2].target_speed_kph = 60;
        return segs;
    }

public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Next / Prev Tests");

        suite->addTest("prev merges the segment back and keeps the next known point", []() {
            auto segs = roadbook();
            // "next" pressed too early, at 600: boundary 1 moved to 600.
            ASSERT_TRUE(retimeSegmentBoundaryForward(segs, 0, 600, 600000));
            ASSERT_EQ(segmentStartStageCounts(segs, 2), 3000);   // next known point
            // prev: segment 0 runs on to that known point, segment 1 is empty.
            ASSERT_TRUE(mergeSegmentBack(segs, 1, 600000));
            ASSERT_NEAR(segs[0].distance_counts, 3000.0, 0.001);
            ASSERT_NEAR(segs[1].distance_counts, 0.0, 0.001);
            ASSERT_EQ(segmentStartStageCounts(segs, 2), 3000);   // unmoved
            // "next" at the real change point, 1200: back to two segments
            // with the known point still at 3000.
            ASSERT_TRUE(retimeSegmentBoundaryForward(segs, 0, 1200, 600000));
            ASSERT_NEAR(segs[0].distance_counts, 1200.0, 0.001);
            ASSERT_NEAR(segs[1].distance_counts, 1800.0, 0.001);
            ASSERT_EQ(segmentStartStageCounts(segs, 2), 3000);
            return true;
        });

        suite->addTest("prev refuses on the first segment", []() {
            auto segs = roadbook();
            ASSERT_FALSE(mergeSegmentBack(segs, 0, 600000));
            ASSERT_NEAR(segs[0].distance_counts, 1000.0, 0.001);
            return true;
        });

        suite->addTest("re-entering a segment puts its baseline back at its start", []() {
            for (bool two_wheel : {true, false}) {
                RallyState s;
                s.counters = two_wheel;
                s.total_distance_adjust_cm = -500;
                rebaseSegmentAt(s, 10000, 10040, 1700);
                ASSERT_EQ(calculateDistanceCounts(s, 10000, 10040,
                                                  s.segment_start_cntr1, s.segment_start_cntr2), 1700);
                // The correction as it stands now belongs to the stage, not
                // to the segment being re-entered.
                ASSERT_EQ(s.segment_start_adjust_cm, -500);
            }
            return true;
        });

        suite->addTest("the segment at a stage distance crosses only auto boundaries", []() {
            auto segs = roadbook();               // 1000 / 2000 / 1500
            for (auto& s : segs) s.autoNext = true;
            ASSERT_EQ(segmentIndexAtStageCounts(segs, 0), 0);
            ASSERT_EQ(segmentIndexAtStageCounts(segs, 999), 0);
            ASSERT_EQ(segmentIndexAtStageCounts(segs, 1000), 1);
            ASSERT_EQ(segmentIndexAtStageCounts(segs, 3500), 2);
            ASSERT_EQ(segmentIndexAtStageCounts(segs, 9999), 2);   // past the end: last
            // A manual boundary waits for "next".
            segs[0].autoNext = false;
            ASSERT_EQ(segmentIndexAtStageCounts(segs, 3500), 0);
            ASSERT_EQ(segmentIndexAtStageCounts({}, 100), -1);
            return true;
        });

        suite->addTest("next and prev are live anywhere in a segment", []() {
            RallyState s;
            s.stage_segments = roadbook();
            s.segment_current_number = -1;
            ASSERT_FALSE(nextAvailable(s));
            ASSERT_FALSE(prevAvailable(s));
            s.segment_current_number = 0;
            ASSERT_TRUE(nextAvailable(s));
            ASSERT_FALSE(prevAvailable(s));
            s.segment_current_number = 1;
            ASSERT_TRUE(nextAvailable(s));
            ASSERT_TRUE(prevAvailable(s));
            s.segment_current_number = 2;
            ASSERT_FALSE(nextAvailable(s));
            ASSERT_TRUE(prevAvailable(s));
            return true;
        });

        return suite;
    }
};

#endif // TEST_NEXT_PREV_H
