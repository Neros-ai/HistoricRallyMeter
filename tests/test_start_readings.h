#ifndef TEST_START_READINGS_H
#define TEST_START_READINGS_H

#include "test_framework.h"
#include "../calculations.h"
#include "../config_file.h"
#include "../rally_state.h"
#include "../stage_phase.h"
#include <cstdio>

// Implicit-state audit item 1: a saved start reading of 0 is a real reading (the box was
// switched on with the car standing), not "never set". The start-up check runs once only.
class TestStartReadings {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Start Readings Tests");

        suite->addTest("a brand-new box takes the live counts as every start reading", []() {
            RallyState s;
            ASSERT_TRUE(recordStartReadingsIfNew(s, 500, 600));
            ASSERT_EQ(s.total_start_cntr1, 500u);
            ASSERT_EQ(s.trip_start_cntr2, 600u);
            ASSERT_EQ(s.segment_start_cntr1, 500u);
            ASSERT_EQ(s.odometer_start_cntr2, 600u);
            ASSERT_TRUE(s.start_readings_recorded);
            return true;
        });

        suite->addTest("0/0 start readings stay when the box program restarts mid-drive", []() {
            RallyState s;
            recordStartReadingsIfNew(s, 0, 0);          // switched on standing: readings are really 0/0
            ASSERT_TRUE(s.start_readings_recorded);
            const std::string f = "test_start_readings.json";
            ConfigFile::save(s, f);
            RallyState after;                            // the box program restarts, chips keep counting
            ConfigFile::load(after, f);
            std::remove(f.c_str());
            ASSERT_FALSE(recordStartReadingsIfNew(after, 40000, 40000));
            ASSERT_EQ(after.total_start_cntr1, 0u);
            ASSERT_EQ(after.trip_start_cntr1, 0u);
            ASSERT_EQ(after.segment_start_cntr2, 0u);
            ASSERT_EQ(after.odometer_start_cntr1, 0u);
            return true;
        });

        suite->addTest("a settings file from before the fix behaves as before, once", []() {
            RallyState old;                              // no start_readings_recorded in the file
            ASSERT_FALSE(old.start_readings_recorded);
            ASSERT_TRUE(recordStartReadingsIfNew(old, 700, 800));
            ASSERT_EQ(old.total_start_cntr1, 700u);
            ASSERT_FALSE(recordStartReadingsIfNew(old, 900, 900));
            ASSERT_EQ(old.total_start_cntr1, 700u);
            return true;
        });

        suite->addTest("a power-loss restart records the start readings", []() {
            RallyState s;
            restartDistancesAfterPowerLoss(s, 0, 0, 1000);
            ASSERT_TRUE(s.start_readings_recorded);
            return true;
        });

        // Audit item 4: next is dead on a finished stage, as prev is.
        suite->addTest("next is unavailable once the stage is finished", []() {
            RallyState s;
            Segment a; a.distance_m = 1000; a.target_speed_kph = 40; a.autoNext = false;
            Segment b = a;
            s.stage_segments = {a, b};
            s.segment_current_number = 0;
            s.stage_complete = false;
            ASSERT_TRUE(nextAvailable(s));
            s.stage_complete = true;
            ASSERT_FALSE(nextAvailable(s));
            return true;
        });

        suite->addTest("a timed start needs at least one full row (a distance and a speed)", []() {
            std::vector<Segment> none;
            ASSERT_FALSE(stageHasFullRow(none));
            Segment half; half.distance_m = 1000;               // no speed
            ASSERT_FALSE(stageHasFullRow({half}));
            Segment full; full.distance_m = 1000; full.target_speed_kph = 40;
            ASSERT_TRUE(stageHasFullRow({half, full}));
            return true;
        });

        suite->addTest("early departure armed is true only for the early kind", []() {
            RallyState s;
            ASSERT_FALSE(earlyDepartureArmed(s));
            s.auto_start_rally_time_s = 100; s.auto_start_early_departure = true;
            ASSERT_TRUE(earlyDepartureArmed(s));
            s.auto_start_early_departure = false;
            ASSERT_FALSE(earlyDepartureArmed(s));
            return true;
        });

        suite->addTest("a timed start recalls its own stage and fires only with a full row", []() {
            RallyState s;
            s.auto_slot = 2; s.last_memory_slot = 5;
            ASSERT_TRUE(timedStartNeedsRecall(s));
            ASSERT_FALSE(timedStartMayFire(s));
            s.last_memory_slot = 2;
            ASSERT_FALSE(timedStartNeedsRecall(s));
            ASSERT_FALSE(timedStartMayFire(s));           // loaded, but no row
            Segment full; full.distance_m = 1000; full.target_speed_kph = 40;
            s.segments = {full};
            ASSERT_TRUE(timedStartMayFire(s));
            s.auto_slot = 0;
            ASSERT_FALSE(timedStartNeedsRecall(s));
            return true;
        });

        suite->addTest("clearing the armed stage or all cancels a timed start, not an early departure", []() {
            RallyState s;
            s.auto_start_rally_time_s = 100; s.auto_slot = 3; s.last_memory_slot = 3;
            ASSERT_TRUE(timedStartDiesWithStage(s, false));
            s.last_memory_slot = 4;
            ASSERT_FALSE(timedStartDiesWithStage(s, false));
            ASSERT_TRUE(timedStartDiesWithStage(s, true));
            s.auto_start_early_departure = true;
            ASSERT_FALSE(timedStartDiesWithStage(s, true));
            s.auto_start_early_departure = false; s.auto_start_rally_time_s = 0;
            ASSERT_FALSE(timedStartDiesWithStage(s, true));
            return true;
        });

        suite->addTest("a time earlier than now means tomorrow, across midnight too", []() {
            const int64_t day = 24LL * 3600 * 1000;
            ASSERT_EQ(timedStartTarget(5000, 1000), 5000);            // later today: unchanged
            ASSERT_EQ(timedStartTarget(1000, 1000), 1000 + day);      // equal to now: tomorrow
            ASSERT_EQ(timedStartTarget(500, 1000), 500 + day);        // earlier: tomorrow
            const int64_t t2358 = 23LL * 3600000 + 58 * 60000;        // 23:58 now, 00:01 typed (as today's 00:01)
            ASSERT_EQ(timedStartTarget(60000, t2358), 60000 + day);
            return true;
        });

        suite->addTest("next-minute is refused in italian only", []() {
            RallyState s;
            s.rally_type = RALLY_GREEK; ASSERT_TRUE(nextMinuteAllowed(s));
            s.rally_type = RALLY_SURREY; ASSERT_TRUE(nextMinuteAllowed(s));
            s.rally_type = RALLY_ITALIAN; ASSERT_FALSE(nextMinuteAllowed(s));
            return true;
        });

        return suite;
    }
};

#endif
