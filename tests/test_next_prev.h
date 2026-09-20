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

        // RB-SEG-07. A press the NEXT segment can absorb is the ordinary
        // case and must not change: it shrinks, the stage total is unmoved.
        suite->addTest("a press the next segment can absorb leaves the stage length alone", []() {
            auto segs = roadbook();                 // 1000 / 2000 / 1500 = 4500
            // In segment 1 (Auto off), pressed at 2500 -- well past its own
            // 1000, but inside segment 2, which ends at 3000.
            ASSERT_TRUE(retimeSegmentBoundaryForward(segs, 0, 2500, 600000));
            ASSERT_NEAR(segs[0].distance_counts, 2500.0, 0.001);
            ASSERT_NEAR(segs[1].distance_counts,  500.0, 0.001);
            ASSERT_NEAR(segs[2].distance_counts, 1500.0, 0.001);
            double total = segs[0].distance_counts + segs[1].distance_counts
                         + segs[2].distance_counts;
            ASSERT_NEAR(total, 4500.0, 0.001);      // finish unmoved
            return true;
        });

        // RB-SEG-07. Past the END of the next segment it cannot absorb the
        // press. It used to be clamped to zero and the remainder dropped --
        // the stage total grew by less than the overshoot, the missed
        // segment's speed was never driven, and every later change point
        // moved. Owner's ruling: keep it whole and let the stage run long.
        suite->addTest("a press past the next segment keeps that segment whole", []() {
            auto segs = roadbook();                 // 1000 / 2000 / 1500 = 4500
            // In segment 1, pressed at 3500 -- past segment 2's end at 3000.
            ASSERT_TRUE(retimeSegmentBoundaryForward(segs, 0, 3500, 600000));
            ASSERT_NEAR(segs[0].distance_counts, 3500.0, 0.001);
            ASSERT_NEAR(segs[1].distance_counts, 2000.0, 0.001);  // untouched
            ASSERT_NEAR(segs[2].distance_counts, 1500.0, 0.001);
            double total = segs[0].distance_counts + segs[1].distance_counts
                         + segs[2].distance_counts;
            ASSERT_NEAR(total, 7000.0, 0.001);      // 3500 + the rest, in full
            // Nothing is thrown away: the two later segments keep their whole
            // roadbook lengths, so their speeds are still driven.
            ASSERT_EQ(segmentStartStageCounts(segs, 1), 3500);
            ASSERT_EQ(segmentStartStageCounts(segs, 2), 5500);
            return true;
        });

        // RB-NAV-15. "next" zeroes Trip; undoing it has to put Trip back to
        // the segment's own baseline, or Trip reads short for the rest of the
        // segment -- and Trip is what the co-pilot judges the next change
        // point by.
        suite->addTest("prev puts Trip back to the segment it lands in", []() {
            RallyState state;
            state.calibration = 600000;
            state.counters = false;
            state.stage_segments = roadbook();
            state.total_start_time_ms = 1000;
            // A "next" was pressed in segment 0 at 600 and moved us to 1.
            ASSERT_TRUE(retimeSegmentBoundaryForward(state.stage_segments, 0, 600,
                                                     state.calibration));
            state.segment_current_number = 1;
            state.undo_valid = true;
            state.undo_automatic = false;
            state.undo_from_index = 0;
            state.undo_from_counts = 1000;      // segment 0 before the press
            state.undo_to_counts   = 2000;      // segment 1 before the press
            state.undo_stage_start_ms = state.total_start_time_ms;
            // Trip was zeroed at the press and has been counting since.
            state.trip_start_cntr1 = 900;
            state.trip_start_cntr2 = 900;
            state.trip_start_time_ms = 55555;
            state.trip_distance_adjust_cm = 250;

            // prev, with the car at 800 counts into the stage.
            ASSERT_TRUE(undoSegmentChange(state, 800, 800, 800));
            ASSERT_EQ(state.segment_current_number, 0);
            // Trip now measures from the segment, not from the mistaken press.
            ASSERT_EQ(state.trip_start_cntr1, state.segment_start_cntr1);
            ASSERT_EQ(state.trip_start_cntr2, state.segment_start_cntr2);
            ASSERT_EQ(state.trip_start_time_ms, state.segment_start_time_ms);
            ASSERT_EQ(state.trip_distance_adjust_cm, 0);
            return true;
        });

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

        suite->addTest("prev's merged segment takes the undone segment's auto-advance", []() {
            auto segs = roadbook();
            segs[0].autoNext = true;    // auto change, fired too early
            segs[1].autoNext = false;   // ends at a timing point: manual
            ASSERT_TRUE(mergeSegmentBack(segs, 1, 600000));
            ASSERT_FALSE(segs[0].autoNext);
            segs = roadbook();
            segs[0].autoNext = false;
            segs[1].autoNext = true;
            ASSERT_TRUE(mergeSegmentBack(segs, 1, 600000));
            ASSERT_TRUE(segs[0].autoNext);
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

        suite->addTest("next is live anywhere; prev only right after a change, once", []() {
            RallyState s;
            s.stage_segments = roadbook();
            s.segment_current_number = -1;
            ASSERT_FALSE(nextAvailable(s));
            ASSERT_FALSE(prevAvailable(s));
            s.segment_current_number = 0;
            ASSERT_TRUE(nextAvailable(s));
            ASSERT_FALSE(prevAvailable(s));          // nothing to undo
            recordSegmentChange(s, 0, false);
            s.segment_current_number = 1;
            ASSERT_TRUE(prevAvailable(s));
            s.segment_current_number = 2;            // moved on: gone
            ASSERT_FALSE(prevAvailable(s));
            ASSERT_FALSE(nextAvailable(s));
            s.segment_current_number = 1;
            s.total_start_time_ms = 999;             // another stage: gone
            ASSERT_FALSE(prevAvailable(s));
            return true;
        });

        // 30 km/h to 1000, 45 to 3000, 60 to 4500 (counts). The owner's
        // examples, with "next" at 500 and the roadbook change at 1000.
        suite->addTest("prev undoes a mistaken next: the roadbook point comes back", []() {
            RallyState s;
            s.stage_segments = roadbook();
            s.segment_current_number = 0;
            recordSegmentChange(s, 0, false);
            ASSERT_TRUE(retimeSegmentBoundaryForward(s.stage_segments, 0, 500, 600000));
            s.segment_current_number = 1;
            // prev at 700: short of 1000, so back in the earlier segment.
            ASSERT_TRUE(undoSegmentChange(s, 10700, 10700, 700));
            ASSERT_EQ(s.segment_current_number, 0);
            ASSERT_NEAR(s.stage_segments[0].distance_counts, 1000.0, 0.001);
            ASSERT_NEAR(s.stage_segments[1].distance_counts, 2000.0, 0.001);
            ASSERT_EQ(calculateDistanceCounts(s, 10700, 10700,
                s.segment_start_cntr1, s.segment_start_cntr2), 700);
            ASSERT_FALSE(prevAvailable(s));          // once only
            return true;
        });

        suite->addTest("prev after the roadbook point keeps the later speed, change at the point", []() {
            RallyState s;
            s.stage_segments = roadbook();
            s.segment_current_number = 0;
            recordSegmentChange(s, 0, false);
            ASSERT_TRUE(retimeSegmentBoundaryForward(s.stage_segments, 0, 500, 600000));
            s.segment_current_number = 1;
            // Noticed only at 1100: past 1000, so it stays in segment 1,
            // counted from 1000.
            ASSERT_TRUE(undoSegmentChange(s, 11100, 11100, 1100));
            ASSERT_EQ(s.segment_current_number, 1);
            ASSERT_NEAR(s.stage_segments[0].distance_counts, 1000.0, 0.001);
            ASSERT_EQ(calculateDistanceCounts(s, 11100, 11100,
                s.segment_start_cntr1, s.segment_start_cntr2), 100);
            return true;
        });

        suite->addTest("prev undoes an automatic change: the earlier speed holds", []() {
            RallyState s;
            s.stage_segments = roadbook();
            for (auto& seg : s.stage_segments) seg.autoNext = true;
            s.segment_current_number = 0;
            recordSegmentChange(s, 0, true);          // auto-advance at 1000
            s.segment_current_number = 1;
            ASSERT_TRUE(undoSegmentChange(s, 11200, 11200, 1200));
            ASSERT_EQ(s.segment_current_number, 0);
            // Runs on to the next known point, 3000, not re-firing at 1000.
            ASSERT_NEAR(s.stage_segments[0].distance_counts, 3000.0, 0.001);
            ASSERT_NEAR(s.stage_segments[1].distance_counts, 0.0, 0.001);
            ASSERT_EQ(segmentStartStageCounts(s.stage_segments, 2), 3000);
            ASSERT_EQ(calculateDistanceCounts(s, 11200, 11200,
                s.segment_start_cntr1, s.segment_start_cntr2), 1200);
            return true;
        });

        return suite;
    }
};

#endif // TEST_NEXT_PREV_H
