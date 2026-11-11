#ifndef TEST_STAGE_PHASE_H
#define TEST_STAGE_PHASE_H

#include "test_framework.h"
#include "../stage_phase.h"
#include "../calculations.h"
#include "../config_file.h"
#include "../rally_state.h"
#include "../sector.h"
#include "../surrey.h"
#include <cstdio>

// RB-NAV-66 / 67, RB-DEV-17: the stage phase in one place, and the keys that read it.
class TestStagePhase {
    static Segment row(double m, double kph) {
        Segment g; g.distance_m = m; g.target_speed_kph = kph; return g;
    }
    static RallyState running(int rows, long current) {
        RallyState s;
        for (int i = 0; i < rows; i++) s.stage_segments.push_back(row(1000, 40));
        s.segments = s.stage_segments;
        s.segment_current_number = current;
        s.stage_complete = false;
        return s;
    }
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Stage Phase Tests");

        suite->addTest("the six phases, from what the box stores", []() {
            RallyState s;
            ASSERT_TRUE(stagePhase(s) == StagePhase::Idle);
            s.auto_start_rally_time_s = 1000;
            ASSERT_TRUE(stagePhase(s) == StagePhase::Scheduled);
            s = running(3, 1);
            ASSERT_TRUE(stagePhase(s) == StagePhase::Running);
            s.auto_start_rally_time_s = 1000;
            ASSERT_TRUE(stagePhase(s) == StagePhase::ScheduledOverRunning);
            s.auto_start_early_departure = true;
            ASSERT_TRUE(stagePhase(s) == StagePhase::EarlyCountdown);
            s = running(3, 2);
            s.stage_complete = true;
            ASSERT_TRUE(stagePhase(s) == StagePhase::Finished);
            s.auto_start_rally_time_s = 1000;   // a scheduled start over a finished stage: nothing runs
            ASSERT_TRUE(stagePhase(s) == StagePhase::Scheduled);
            return true;
        });

        suite->addTest("the go key reads abort only for a running stage or an early countdown", []() {
            RallyState s;
            ASSERT_FALSE(goKeyAborts(s));                       // idle
            s.auto_start_rally_time_s = 1000;
            ASSERT_FALSE(goKeyAborts(s));                       // scheduled, nothing running: go starts now
            s = running(3, 1);
            ASSERT_TRUE(goKeyAborts(s));                        // running
            s.auto_start_rally_time_s = 1000;
            ASSERT_TRUE(goKeyAborts(s));                        // scheduled over a running stage
            s.auto_start_early_departure = true;
            ASSERT_TRUE(goKeyAborts(s));                        // early-departure countdown
            return true;
        });

        suite->addTest("prev is dead on a finished stage: no speed change is taken back", []() {
            RallyState s = running(3, 0);
            recordSegmentChange(s, 0, true);
            s.segment_current_number = 1;
            ASSERT_TRUE(prevKeyEnabled(s, 0, 0));               // running: the change can be undone
            s.stage_complete = true;                            // driven out
            ASSERT_FALSE(prevKeyEnabled(s, 0, 0));
            return true;
        });

        suite->addTest("a finished stage reads go, not abort", []() {
            RallyState s = running(3, 2);
            s.stage_complete = true;                            // driven out, figures still on screen
            ASSERT_FALSE(goKeyAborts(s));
            ASSERT_TRUE(stageAbortable(s));                     // an abort sent anyway still clears it
            s.auto_start_rally_time_s = 1000;                   // and with a scheduled start waiting
            ASSERT_FALSE(goKeyAborts(s));
            return true;
        });

        suite->addTest("next is dead throughout the early-departure countdown", []() {
            RallyState s = running(3, 0);
            ASSERT_TRUE(nextKeyEnabled(s));                     // running, a row follows
            s.auto_start_rally_time_s = 1000;
            s.auto_start_early_departure = true;
            ASSERT_TRUE(nextAvailable(s));                      // a row does follow...
            ASSERT_FALSE(nextKeyEnabled(s));                    // ...but the key is dead
            s.auto_start_rally_time_s = 0;                      // the minute comes: the stage runs
            s.auto_start_early_departure = false;
            ASSERT_TRUE(nextKeyEnabled(s));
            return true;
        });

        suite->addTest("next cannot finish a one-row surrey stage during the countdown", []() {
            RallyState s = running(1, 0);
            s.rally_type = 1;
            ASSERT_TRUE(surreyFinishAvailable(s));
            ASSERT_TRUE(nextKeyEnabled(s));                     // running: the finish press
            s.auto_start_rally_time_s = 1000;
            s.auto_start_early_departure = true;
            ASSERT_FALSE(nextKeyEnabled(s));
            return true;
        });

        suite->addTest("next elsewhere is as before", []() {
            RallyState s;
            ASSERT_FALSE(nextKeyEnabled(s));                    // idle
            s = running(3, 2);
            ASSERT_FALSE(nextKeyEnabled(s));                    // last row, greek: nothing follows
            s = running(3, 1);
            s.auto_start_rally_time_s = 1000;                   // scheduled over a running stage:
            ASSERT_TRUE(nextKeyEnabled(s));                     // next still belongs to that stage
            return true;
        });

        suite->addTest("how the stage started survives a restart", []() {
            const char* path = "test_stage_started.json";
            RallyState a = running(3, 1);
            a.started_by_auto = true;
            a.auto_fired_time_s = 215999999;
            a.auto_slot = 7;
            ConfigFile::save(a, path);
            RallyState b;
            ConfigFile::load(b, path);
            std::remove(path);
            ASSERT_TRUE(b.started_by_auto);
            ASSERT_TRUE(b.auto_fired_time_s == 215999999ULL);
            ASSERT_EQ(b.auto_slot, 7);
            return true;
        });

        suite->addTest("a manual start survives a restart as manual", []() {
            const char* path = "test_stage_started_manual.json";
            RallyState a = running(3, 1);
            a.auto_fired_time_s = 12345;
            a.auto_slot = 3;
            ConfigFile::save(a, path);
            RallyState b;
            b.started_by_auto = true;
            ConfigFile::load(b, path);
            std::remove(path);
            ASSERT_FALSE(b.started_by_auto);
            ASSERT_TRUE(b.auto_fired_time_s == 12345ULL);
            ASSERT_EQ(b.auto_slot, 3);
            return true;
        });

        suite->addTest("prev is live after an italian tube press, for box and phone alike", []() {
            RallyState s = running(3, 0);
            s.rally_type = 2;
            for (Segment& g : s.stage_segments) g.type = SEG_NAVI;
            ASSERT_FALSE(prevKeyEnabled(s, 0, 0));              // nothing pressed yet
            ASSERT_TRUE(applySectorNext(s, 1000, 1000, 60000));
            ASSERT_FALSE(prevAvailable(s));                     // not the greek undo...
            ASSERT_TRUE(sectorUndoAvailable(s));                // ...the italian one
            ASSERT_TRUE(prevKeyEnabled(s, 1000, 1000));
            ASSERT_TRUE(undoSectorTube(s));
            ASSERT_FALSE(prevKeyEnabled(s, 1000, 1000));        // once only
            return true;
        });

        suite->addTest("prev is live after a greek speed change", []() {
            RallyState s = running(3, 0);
            ASSERT_FALSE(prevKeyEnabled(s, 0, 0));
            recordSegmentChange(s, 0, false);
            s.segment_current_number = 1;
            ASSERT_TRUE(prevKeyEnabled(s, 0, 0));
            return true;
        });

        suite->addTest("an autostart still ahead at startup is kept", []() {
            RallyState s;
            s.auto_start_rally_time_s = 1000;                   // epoch + 1000 s
            ASSERT_FALSE(dropMissedAutoStart(s, 5000 + 999999, 5000));
            ASSERT_TRUE(s.auto_start_rally_time_s == 1000ULL);
            RallyState none;
            ASSERT_FALSE(dropMissedAutoStart(none, 123456789, 5000));       // nothing armed
            return true;
        });

        suite->addTest("a scheduled start that passed while the box was down is dropped", []() {
            RallyState s;
            s.auto_start_rally_time_s = 1000;
            ASSERT_TRUE(dropMissedAutoStart(s, 5000 + 1000000, 5000));    // the armed second itself
            ASSERT_TRUE(stagePhase(s) == StagePhase::Idle);
            ASSERT_FALSE(goKeyAborts(s));                       // the crew press stage go
            return true;
        });

        suite->addTest("a missed early departure goes to idle, never a late start", []() {
            RallyState s = running(3, 0);                       // distance counting since the arm
            s.auto_start_rally_time_s = 1000;
            s.auto_start_early_departure = true;
            s.last_pc_error.valid = true;
            const uint64_t c1 = s.total_start_cntr1 = 4321;
            const int64_t t0 = s.total_start_time_ms = 777;
            ASSERT_TRUE(dropMissedAutoStart(s, 5000 + 1000500, 5000));    // half a second late: still dropped
            ASSERT_TRUE(stagePhase(s) == StagePhase::Idle);
            ASSERT_FALSE(s.auto_start_early_departure);
            ASSERT_FALSE(s.last_pc_error.valid);
            ASSERT_FALSE(stageLive(s));                         // stages may be edited again
            ASSERT_TRUE(s.total_start_cntr1 == c1 && s.total_start_time_ms == t0);   // Total keeps its reading
            return true;
        });

        suite->addTest("a missed start over a running stage ends that stage too", []() {
            RallyState s = running(3, 1);
            s.auto_start_rally_time_s = 1000;
            ASSERT_TRUE(dropMissedAutoStart(s, 5000 + 2000000, 5000));
            ASSERT_TRUE(stagePhase(s) == StagePhase::Idle);
            return true;
        });

        return suite;
    }
};

#endif // TEST_STAGE_PHASE_H
