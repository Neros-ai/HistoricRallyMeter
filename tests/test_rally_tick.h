#ifndef TEST_RALLY_TICK_H
#define TEST_RALLY_TICK_H

#include "test_framework.h"
#include "../rally_tick.h"
#include "../calculations.h"
#include "../rally_state.h"
#include "../sector.h"
#include "../surrey.h"
#include "../stage_phase.h"
#include <ctime>
#include <vector>

// The widget-free tick (rally_tick.h): the decisions that used to be made inside the driver and
// co-pilot display updaters. These pin the moved behaviour against the existing calculations --
// the tick must give exactly what those functions give, at the same moments.
// Calibration 1,000,000: one count is one metre.
class TestRallyTick {
    static Segment leg(double m, double kph) {
        Segment g; g.distance_m = m; g.target_speed_kph = kph; return g;
    }
    // Two 5 km rows at 36 km/h (10 m/s), running from t = 0 and count 0, in row 1.
    static RallyState running(int rally_type = RALLY_GREEK) {
        RallyState s;
        s.calibration = 1000000;
        s.counters = true;
        s.rally_type = rally_type;
        s.segments = { leg(5000, 36), leg(5000, 36) };
        s.stage_segments = s.segments;
        s.total_start_cntr1 = s.total_start_cntr2 = 0;
        s.trip_start_cntr1 = s.trip_start_cntr2 = 0;
        s.segment_start_cntr1 = s.segment_start_cntr2 = 0;
        s.total_start_time_ms = s.trip_start_time_ms = s.segment_start_time_ms = 0;
        s.segment_current_number = 0;
        s.stage_complete = false;
        return s;
    }
    static RallyState idle(int rally_type = RALLY_GREEK) {
        RallyState s = running(rally_type);
        s.segment_current_number = -1;
        s.stage_complete = true;
        return s;
    }
    // The Mugello sector of test_sector.h, running from t = 0 in its first (navi) row.
    static Segment row(int type, double m, double pc_s, int ref, double kph = 0.0) {
        Segment s; s.type = type; s.distance_m = m; s.pc_duration_s = pc_s; s.ref = ref;
        s.target_speed_kph = kph;
        return s;
    }
    static RallyState sector() {
        RallyState s;
        s.calibration = 1000000;
        s.counters = true;
        s.rally_type = RALLY_ITALIAN;
        s.segments = { row(SEG_NAVI, 380, 0, 0, 30.0), row(SEG_PC, 180, 31, 1), row(SEG_PC, 60, 10, 2),
                       row(SEG_PC, 110, 19, 3), row(SEG_NAVI, 16500, 0, 0, 30.0), row(SEG_PC, 50, 9, 4),
                       row(SEG_PC, 310, 46, 5) };
        applySectorRules(s.segments, 10800);
        s.stage_segments = s.segments;
        s.segment_current_number = 0;
        s.total_start_cntr1 = s.total_start_cntr2 = 0;
        s.trip_start_cntr1 = s.trip_start_cntr2 = 0;
        s.segment_start_cntr1 = s.segment_start_cntr2 = 0;
        s.total_start_time_ms = s.trip_start_time_ms = s.segment_start_time_ms = 0;
        s.stage_complete = false;
        s.sector_card_ms = 10800 * 1000L;
        s.sector_start_ms = 0;
        return s;
    }
    static CounterPoll at(uint64_t c, int64_t ms = 0) { return CounterPoll{c, c, ms}; }
    // One driver half with no autostart pending and no hold.
    static void drive(const RallyState& s, uint64_t c, int64_t now_ms, TickMemory& mem, TickDecision& out,
                      int64_t remaining_ms = 0, bool hold = false) {
        tickDrive(s, at(c, now_ms), at(c, now_ms), at(c, now_ms), now_ms, remaining_ms, hold, mem, out);
    }
    static bool sameTone(const ToneCadence& a, const ToneCadence& b) {
        return a.tone_ms == b.tone_ms && a.silence_ms == b.silence_ms && a.freq_hz == b.freq_hz
            && a.triangle == b.triangle && a.square == b.square;
    }
    // A rally time whose local time of day is hh:mm:ss.
    static int64_t clockMs(int hh, int mm, int ss) {
        time_t t = 1750000000;
        struct tm lt = *localtime(&t);
        lt.tm_hour = hh; lt.tm_min = mm; lt.tm_sec = ss;
        return static_cast<int64_t>(mktime(&lt)) * 1000;
    }
    // Stage 1 loaded: starts at 10:30:00, 6200 m from where Total was zeroed.
    static void loadStage(RallyState& s) {
        s.last_memory_slot = 1;
        s.memory_slots[0].start_time_s = 10 * 3600 + 30 * 60;
        s.memory_slots[0].start_distance_m = 6200;
    }
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Rally Tick Tests");

        // ---- time of day, Stage arrival ----

        suite->addTest("timeOfDaySeconds is the local hour, minute and second of the clock", []() {
            long tod = -1;
            ASSERT_TRUE(timeOfDaySeconds(clockMs(10, 30, 45), &tod));
            ASSERT_EQ(tod, 10 * 3600L + 30 * 60L + 45L);
            ASSERT_TRUE(timeOfDaySeconds(clockMs(10, 30, 45) + 999, &tod));   // the milliseconds are dropped
            ASSERT_EQ(tod, 10 * 3600L + 30 * 60L + 45L);
            ASSERT_TRUE(timeOfDaySeconds(clockMs(0, 0, 0), &tod));
            ASSERT_EQ(tod, 0L);
            return true;
        });

        suite->addTest("stageArrivalNow is stageArrival on the metres to the start and the minutes to go", []() {
            RallyState s = idle();
            loadStage(s);
            const int64_t now = clockMs(10, 15, 15);   // 14 min 45 s before the start
            const StageArrivalNow a = stageArrivalNow(s, 1000, 1000, now);   // 1000 m of Total driven
            const StageArrival want = stageArrival(static_cast<double>(metresToStageStart(s, 1000, 1000, 6200)), 14.75);
            ASSERT_TRUE(a.hasStartTime);
            ASSERT_NEAR(a.minutesToStart, 14.75, 1e-9);
            ASSERT_TRUE(want.valid);
            ASSERT_EQ(a.valid, want.valid);
            ASSERT_EQ(a.minutes, want.minutes);
            ASSERT_EQ(a.speed_kph, want.speed_kph);
            ASSERT_EQ(a.minutes, 14);
            return true;
        });

        suite->addTest("stageArrivalNow: nothing without a loaded stage or a start time; blank inside the last minute", []() {
            RallyState s = idle();
            ASSERT_FALSE(stageArrivalNow(s, 0, 0, clockMs(10, 0, 0)).hasStartTime);   // no stage loaded
            s.last_memory_slot = 1;                                                    // loaded, no start time
            ASSERT_FALSE(stageArrivalNow(s, 0, 0, clockMs(10, 0, 0)).hasStartTime);
            ASSERT_FALSE(stageArrivalNow(s, 0, 0, clockMs(10, 0, 0)).valid);
            loadStage(s);
            const StageArrivalNow last = stageArrivalNow(s, 0, 0, clockMs(10, 29, 30));
            ASSERT_TRUE(last.hasStartTime);
            ASSERT_FALSE(last.valid);
            ASSERT_NEAR(last.minutesToStart, 0.5, 1e-9);
            const StageArrivalNow gone = stageArrivalNow(s, 0, 0, clockMs(10, 31, 0));
            ASSERT_FALSE(gone.valid);
            ASSERT_NEAR(gone.minutesToStart, -1.0, 1e-9);
            return true;
        });

        suite->addTest("the arrival figure itself has no rally-type gate; the driver screen's rule excludes italian", []() {
            const int64_t now = clockMs(10, 15, 0);
            RallyState g = idle(RALLY_GREEK), it = idle(RALLY_ITALIAN), su = idle(RALLY_SURREY);
            loadStage(g); loadStage(it); loadStage(su);
            const StageArrivalNow ag = stageArrivalNow(g, 0, 0, now), ai = stageArrivalNow(it, 0, 0, now);
            ASSERT_TRUE(ag.valid);
            ASSERT_TRUE(ai.valid);                      // Home and the phone's "arrival" carry it in italian too
            ASSERT_EQ(ai.speed_kph, ag.speed_kph);
            ASSERT_EQ(ai.minutes, ag.minutes);
            ASSERT_TRUE(driverShowsStageArrival(g));
            ASSERT_TRUE(driverShowsStageArrival(su));
            ASSERT_FALSE(driverShowsStageArrival(it));
            return true;
        });

        suite->addTest("tick: one arrival for every screen; the driver's copy only in greek and surrey", []() {
            const int64_t now = clockMs(10, 15, 0);
            const int types[3] = {RALLY_GREEK, RALLY_SURREY, RALLY_ITALIAN};
            for (int t = 0; t < 3; t++) {
                RallyState s = idle(types[t]);
                loadStage(s);
                TickMemory mem; TickDecision out;
                drive(s, 0, now, mem, out);
                const StageArrivalNow want = stageArrivalNow(s, 0, 0, now);
                ASSERT_TRUE(out.arrival.valid);
                ASSERT_EQ(out.arrival.speed_kph, want.speed_kph);
                ASSERT_EQ(out.arrival.minutes, want.minutes);
                if (types[t] == RALLY_ITALIAN) { ASSERT_EQ(out.driverArrivalKph, -1); }
                else { ASSERT_EQ(out.driverArrivalKph, want.speed_kph); }
                ASSERT_TRUE(out.driverStartSpeedKph < 0);   // not the last minute
            }
            return true;
        });

        suite->addTest("tick: in the last minute the driver gets the loaded stage's first speed (not italian)", []() {
            const int types[3] = {RALLY_GREEK, RALLY_SURREY, RALLY_ITALIAN};
            for (int t = 0; t < 3; t++) {
                RallyState s = idle(types[t]);
                loadStage(s);
                TickMemory mem; TickDecision out;
                drive(s, 0, clockMs(10, 29, 0), mem, out);          // exactly a minute: counts
                ASSERT_EQ(out.driverArrivalKph, -1);                 // no arrival speed inside the last minute
                if (types[t] == RALLY_ITALIAN) { ASSERT_TRUE(out.driverStartSpeedKph < 0); }
                else { ASSERT_NEAR(out.driverStartSpeedKph, 36.0, 1e-9); }
                drive(s, 0, clockMs(10, 28, 59), mem, out);          // a second more than a minute
                ASSERT_TRUE(out.driverStartSpeedKph < 0);
                drive(s, 0, clockMs(10, 30, 0), mem, out);           // the start itself: no longer
                ASSERT_TRUE(out.driverStartSpeedKph < 0);
            }
            return true;
        });

        // ---- no stage ----

        suite->addTest("tick, no stage: needle at rest, idle page, tone silent, run bookmark cleared", []() {
            RallyState s = idle();
            TickMemory mem; TickDecision out;
            mem.runSegIndex = 3;
            out.aheadBehindSeconds = 12.0; out.segmentProgress = 0.5; out.inSegment = true;
            out.currentTone.tone_ms = 100; out.currentTone.freq_hz = 440.0;
            drive(s, 700, 50000, mem, out);
            ASSERT_FALSE(out.stageOnGauge);
            ASSERT_NEAR(out.aheadBehindSeconds, 0.0, 1e-12);
            ASSERT_NEAR(out.segmentProgress, 0.0, 1e-12);
            ASSERT_FALSE(out.inSegment);
            ASSERT_FALSE(out.driverTrial.active);
            ASSERT_EQ(out.driverPage, DRIVER_PAGE_GREEK_IDLE);
            ASSERT_FALSE(out.currentTone.sounding());
            ASSERT_TRUE(sameTone(out.currentTone, ToneCadence{}));
            ASSERT_EQ(mem.runSegIndex, -2L);
            return true;
        });

        suite->addTest("tick: a finished stage is off the gauge, in every rally type", []() {
            const int types[3] = {RALLY_GREEK, RALLY_SURREY, RALLY_ITALIAN};
            for (int t = 0; t < 3; t++) {
                RallyState s = running(types[t]);
                s.stage_complete = true;          // finished, the row still current
                TickMemory mem; TickDecision out;
                drive(s, 700, 50000, mem, out);
                ASSERT_FALSE(out.stageOnGauge);
                ASSERT_EQ(out.driverPage, DRIVER_PAGE_GREEK_IDLE);
                ASSERT_NEAR(out.aheadBehindSeconds, 0.0, 1e-12);
            }
            return true;
        });

        // ---- ahead / behind ----

        suite->addTest("tick, greek: ahead/behind is gaugeAheadSeconds on the stage position", []() {
            RallyState s = running();
            TickMemory mem; TickDecision out;
            drive(s, 600, 50000, mem, out);       // 600 m after 50 s at 10 m/s: 10 s ahead
            ASSERT_TRUE(out.stageOnGauge);
            ASSERT_EQ(out.driverPage, DRIVER_PAGE_GREEK_STAGE);
            ASSERT_NEAR(out.aheadBehindSeconds,
                        gaugeAheadSeconds(s, 600, 600, 50000, stagePositionMm(s, 600, 600)), 1e-12);
            ASSERT_NEAR(out.aheadBehindSeconds, 10.0, 1e-6);
            drive(s, 400, 50000, mem, out);       // 400 m: 10 s behind
            ASSERT_NEAR(out.aheadBehindSeconds, -10.0, 1e-6);
            s.ahead_behind_zero_offset_ms = 2500; // the A-B / B-C zero moves the needle
            drive(s, 400, 50000, mem, out);
            ASSERT_NEAR(out.aheadBehindSeconds, -7.5, 1e-6);
            ASSERT_NEAR(out.targetKph, 36.0, 1e-12);
            return true;
        });

        suite->addTest("tick, surrey: the same ahead/behind; the driver's Total is the navi figure", []() {
            RallyState s = running(RALLY_SURREY);
            TickMemory mem; TickDecision out;
            drive(s, 600, 50000, mem, out);
            ASSERT_EQ(out.driverPage, DRIVER_PAGE_GREEK_STAGE);
            ASSERT_NEAR(out.aheadBehindSeconds,
                        gaugeAheadSeconds(s, 600, 600, 50000, stagePositionMm(s, 600, 600)), 1e-12);
            ASSERT_NEAR(out.driverTotalM, static_cast<double>(surreyNaviMetres(s, 600, 600)), 1e-9);
            RallyState g = running(RALLY_GREEK);
            drive(g, 600, 50000, mem, out);
            ASSERT_NEAR(out.driverTotalM, static_cast<double>(out.totalDistM), 1e-9);
            ASSERT_EQ(out.totalDistM, 600L);
            return true;
        });

        suite->addTest("tick, italian navi row: navi page, needle from gaugeAheadSeconds, minutes from the sector clock", []() {
            RallyState s = sector();
            TickMemory mem; TickDecision out;
            drive(s, 200, 30000, mem, out);
            ASSERT_TRUE(out.stageOnGauge);
            ASSERT_FALSE(out.driverTrial.active);
            ASSERT_EQ(out.driverPage, DRIVER_PAGE_ITALIAN_NAVI);
            ASSERT_NEAR(out.aheadBehindSeconds,
                        gaugeAheadSeconds(s, 200, 200, 30000, stagePositionMm(s, 200, 200)), 1e-12);
            bool ok = false;
            const double ahead = sectorAheadSeconds(s, 200, 200, 30000, &ok);
            ASSERT_EQ(out.driverMinutesValid, ok);
            ASSERT_EQ(out.driverMinutesBehind, ahead < 0.0);
            ASSERT_EQ(out.driverMinutes, ahead >= 0.0 ? minutesInHand(ahead) : -minutesInHand(ahead));
            ASSERT_NEAR(out.driverRowS, rowSeconds(s.stage_segments[0]), 1e-12);
            // The chevron follows the countdown to the line on this page.
            bool sec_ok = false;
            const double to_line = sectorSecondsToLine(s, 30000, &sec_ok);
            ASSERT_EQ(out.inSegment, sec_ok);
            ASSERT_NEAR(out.segmentProgress, sec_ok ? finalWindowProgress(to_line) : 0.0, 1e-12);
            return true;
        });

        suite->addTest("tick, italian pc: the page and the needle follow driverTrialView / gaugeAheadSeconds", []() {
            RallyState s = sector();
            s.segment_current_number = 1;          // pc1, its tube pressed at 380 m, 40 s in
            s.trial_start_ms = 40000;
            s.trial_start_m = 380.0;
            TickMemory mem; TickDecision out;
            drive(s, 450, 52000, mem, out);
            const DriverTrialView v = driverTrialView(s, 450, 450, 52000);
            ASSERT_TRUE(v.active);
            ASSERT_EQ(out.driverTrial.active, v.active);
            ASSERT_EQ(out.driverPage, v.active ? DRIVER_PAGE_ITALIAN_TRIAL : DRIVER_PAGE_ITALIAN_NAVI);
            ASSERT_NEAR(out.aheadBehindSeconds,
                        gaugeAheadSeconds(s, 450, 450, 52000, stagePositionMm(s, 450, 450)), 1e-12);
            return true;
        });

        suite->addTest("tick: the hold (autostart on the minute counting down) rests the needle and clears the trial", []() {
            RallyState s = running();
            TickMemory mem; TickDecision out;
            drive(s, 600, 50000, mem, out, 0, /*hold=*/true);
            ASSERT_TRUE(out.stageOnGauge);
            ASSERT_NEAR(out.aheadBehindSeconds, 0.0, 1e-12);
            ASSERT_FALSE(out.driverTrial.active);
            ASSERT_NEAR(out.driverTripAvg, averageSpeedForDisplay(123.0, true), 1e-12);
            ASSERT_NEAR(out.driverTotalAvg, averageSpeedForDisplay(123.0, true), 1e-12);
            return true;
        });

        suite->addTest("tick, greek: the chevron walks the scheduled row on stage time", []() {
            RallyState s = running();
            TickMemory mem; TickDecision out;
            drive(s, 600, 50000, mem, out);
            bool in_row = false;
            const double want = scheduledRowProgress(s.stage_segments, 50.0, &in_row);
            ASSERT_NEAR(out.segmentProgress, want, 1e-12);
            ASSERT_EQ(out.inSegment, in_row);
            return true;
        });

        suite->addTest("tick, greek: the run average starts at a speed change and is kept through a carried row", []() {
            RallyState s = running();
            s.stage_segments[1].changesSpeed = false;      // row 2 carries the speed on
            TickMemory mem; TickDecision out;
            drive(s, 600, 50000, mem, out);
            ASSERT_EQ(mem.runSegIndex, 0L);
            ASSERT_EQ(mem.runStartMs, static_cast<int64_t>(0));
            ASSERT_TRUE(out.driverSubAvgValid);
            ASSERT_NEAR(out.driverSubAvgKph,
                        calculateAverageSpeed(s, 0, 50000, calculateDistanceCounts(s, 600, 600, 0, 0), 0), 1e-9);
            // On to row 2 (carried): the run's start stays at the stage start.
            s.segment_current_number = 1;
            s.segment_start_cntr1 = s.segment_start_cntr2 = 5000;
            s.segment_start_time_ms = 500000;
            drive(s, 5100, 510000, mem, out);
            ASSERT_EQ(mem.runSegIndex, 1L);
            ASSERT_EQ(mem.runStartMs, static_cast<int64_t>(0));
            // The same step where the speed changes: the run restarts at the row.
            TickMemory mem2; TickDecision out2;
            s.segment_current_number = 0;
            drive(s, 600, 50000, mem2, out2);
            s.stage_segments[1].changesSpeed = true;
            s.segment_current_number = 1;
            drive(s, 5100, 510000, mem2, out2);
            ASSERT_EQ(mem2.runStartMs, static_cast<int64_t>(500000));
            ASSERT_EQ(mem2.runStartCntr1, static_cast<uint64_t>(5000));
            return true;
        });

        // ---- speed ----

        suite->addTest("tick: the speed shown is calculateCurrentSpeed, smoothed 2% a tick, dashes with none", []() {
            RallyState s = idle();
            TickMemory mem; TickDecision out;
            const CounterPoll now = at(1100, 11000), tenth = at(1000, 1000);
            const double v = calculateCurrentSpeed(s, now, tenth);
            ASSERT_TRUE(v > 0.0);
            tickDrive(s, now, now, tenth, 10000, 0, false, mem, out);
            ASSERT_NEAR(out.currentSpeed, v, 1e-12);
            ASSERT_NEAR(out.smoothedSpeed, v, 1e-12);                 // the first reading is taken whole
            const CounterPoll faster = at(1300, 11000);
            const double v2 = calculateCurrentSpeed(s, faster, tenth);
            tickDrive(s, faster, faster, tenth, 10000, 0, false, mem, out);
            ASSERT_NEAR(out.smoothedSpeed, 0.02 * v2 + 0.98 * v, 1e-9);
            const double none = calculateCurrentSpeed(s, at(0, 0), at(0, 0));
            if (none < 0) {
                tickDrive(s, at(0, 0), at(0, 0), at(0, 0), 10000, 0, false, mem, out);
                ASSERT_NEAR(out.smoothedSpeed, -1.0, 1e-12);
            }
            return true;
        });

        // ---- tone ----

        suite->addTest("tick: the tone and arrows are the existing decisions, across the ahead/behind range", []() {
            const double errors[] = { -400.0, -40.0, -12.0, -5.0, -1.0, -0.05, 0.0, 0.05, 1.0, 5.0, 12.0, 40.0, 400.0 };
            const int64_t now = 200000;          // 200 s in: on schedule at 2000 m
            for (int type = 1; type <= 2; type++) {
                for (double e : errors) {
                    RallyState s = running();
                    s.tone_enabled = true;
                    s.tone_type_behind = type;
                    s.tone_type_ahead = type;
                    const uint64_t c = static_cast<uint64_t>(2000 + e * 10.0);
                    TickMemory mem; TickDecision out;
                    drive(s, c, now, mem, out);
                    const int64_t mm = stagePositionMm(s, c, c);
                    const double secs = gaugeAheadSeconds(s, c, c, now, mm);
                    ASSERT_NEAR(out.aheadBehindSeconds, secs, 1e-12);
                    ArrowToneState as; SimpleToneState ss;
                    const double dist_m = static_cast<double>(mm) / 1000.0;
                    const bool past = stageDistanceComplete(s.stage_segments, mm);
                    const ArrowToneResult arrow = computeArrowBasedTone(as, secs, 36.0, s.units, dist_m, past);
                    const ToneCadence want = decideToneCadence(true,
                        simpleToneForError(secs, s.tone_type_behind, s.tone_type_ahead), arrow, ss, secs, dist_m, past);
                    ASSERT_EQ(out.arrow.num_arrows, arrow.num_arrows);
                    if (arrow.num_arrows > 0) { ASSERT_EQ(out.arrow.increase_speed, arrow.increase_speed); }
                    ASSERT_TRUE(sameTone(out.currentTone, want));
                    // The latches moved exactly as those functions move them.
                    ASSERT_NEAR(mem.arrowToneState.lastCommittedSeconds, as.lastCommittedSeconds, 1e-12);
                    ASSERT_EQ(mem.arrowToneState.heldRung, as.heldRung);
                    ASSERT_NEAR(mem.simpleToneState.lastCommittedSeconds, ss.lastCommittedSeconds, 1e-12);
                    ASSERT_EQ(mem.simpleToneState.heldRung, ss.heldRung);
                    // Switched off: silent, the arrows unchanged.
                    s.tone_enabled = false;
                    TickMemory mem_off; TickDecision off;
                    drive(s, c, now, mem_off, off);
                    ASSERT_FALSE(off.currentTone.sounding());
                    ASSERT_EQ(off.arrow.num_arrows, arrow.num_arrows);
                }
            }
            return true;
        });

        suite->addTest("tick: well behind sounds and says speed up; on time is silent; the hold is silent", []() {
            RallyState s = running();
            s.tone_enabled = true;
            TickMemory mem; TickDecision out;
            drive(s, 1950, 200000, mem, out);         // 5 s behind, 1950 m into the stage
            ASSERT_TRUE(out.currentTone.sounding());
            ASSERT_TRUE(out.arrow.num_arrows > 0);
            ASSERT_TRUE(out.arrow.increase_speed);
            TickMemory mem2; TickDecision on_time;
            drive(s, 2000, 200000, mem2, on_time);
            ASSERT_FALSE(on_time.currentTone.sounding());
            ASSERT_EQ(on_time.arrow.num_arrows, 0);
            TickMemory mem3; TickDecision held;
            drive(s, 1950, 200000, mem3, held, 0, /*hold=*/true);
            ASSERT_FALSE(held.currentTone.sounding());
            return true;
        });

        // ---- autostart ----

        suite->addTest("tick: an armed autostart fires at its moment, inside the 2 s window, and only once", []() {
            RallyState s = idle();
            s.auto_start_rally_time_s = 1000;
            TickMemory mem; TickDecision out;
            drive(s, 0, 0, mem, out, 10);                         // 10 ms to go: not yet
            ASSERT_TRUE(out.autoStart == AutoStartTick::CountdownShown);
            ASSERT_FALSE(mem.autoStartTriggered);
            drive(s, 0, 0, mem, out, 1);
            ASSERT_TRUE(out.autoStart == AutoStartTick::CountdownShown);
            drive(s, 0, 0, mem, out, 0);                          // the moment
            ASSERT_TRUE(out.autoStart == AutoStartTick::Fire);
            ASSERT_TRUE(mem.autoStartTriggered);
            ASSERT_EQ(out.autoStartRemainingMs, static_cast<int64_t>(0));
            drive(s, 0, 0, mem, out, -10);                        // the next tick: spent, even if still armed
            ASSERT_TRUE(out.autoStart == AutoStartTick::NotPending);
            drive(s, 0, 0, mem, out, -5000);
            ASSERT_TRUE(out.autoStart == AutoStartTick::NotPending);
            // Late, but inside the window: still fires.
            TickMemory late; TickDecision lo;
            drive(s, 0, 0, late, lo, -(AUTO_START_TRIGGER_WINDOW_MS - 1));
            ASSERT_TRUE(lo.autoStart == AutoStartTick::Fire);
            ASSERT_TRUE(late.autoStartTriggered);
            return true;
        });

        suite->addTest("tick: an autostart missed by the whole window is dropped, never started late", []() {
            RallyState s = idle();
            s.auto_start_rally_time_s = 1000;
            TickMemory mem; TickDecision out;
            drive(s, 0, 0, mem, out, -AUTO_START_TRIGGER_WINDOW_MS);
            ASSERT_TRUE(out.autoStart == AutoStartTick::Dropped);
            ASSERT_FALSE(mem.autoStartTriggered);                 // the tick's abort disarms it; no latch
            drive(s, 0, 0, mem, out, -3600000);
            ASSERT_TRUE(out.autoStart == AutoStartTick::Dropped);
            return true;
        });

        suite->addTest("tick: the T- countdown shows inside five minutes, or always for an early departure", []() {
            RallyState s = idle();
            s.auto_start_rally_time_s = 1000;
            TickMemory mem; TickDecision out;
            drive(s, 0, 0, mem, out, AUTO_START_POPUP_WINDOW_MS + 1);
            ASSERT_TRUE(out.autoStart == AutoStartTick::ArmedWaiting);
            drive(s, 0, 0, mem, out, AUTO_START_POPUP_WINDOW_MS);
            ASSERT_TRUE(out.autoStart == AutoStartTick::CountdownShown);
            s.auto_start_early_departure = true;
            drive(s, 0, 0, mem, out, AUTO_START_POPUP_WINDOW_MS + 1);
            ASSERT_TRUE(out.autoStart == AutoStartTick::CountdownShown);
            ASSERT_EQ(out.autoStartRemainingMs, AUTO_START_POPUP_WINDOW_MS + 1);
            s.auto_start_rally_time_s = 0;                       // nothing armed
            drive(s, 0, 0, mem, out, 0);
            ASSERT_TRUE(out.autoStart == AutoStartTick::NotPending);
            ASSERT_FALSE(mem.autoStartTriggered);
            return true;
        });

        suite->addTest("tick: the autostart decision changes no ahead/behind or tone of the same tick", []() {
            // The stage on the gauge is judged first, as it was; the tick acts on Fire afterwards.
            RallyState s = running();
            s.tone_enabled = true;
            s.auto_start_rally_time_s = 1000;
            TickMemory a, b; TickDecision fire, plain;
            drive(s, 1950, 200000, a, fire, 0);
            RallyState t = s;
            t.auto_start_rally_time_s = 0;
            drive(t, 1950, 200000, b, plain, 0);
            ASSERT_TRUE(fire.autoStart == AutoStartTick::Fire);
            ASSERT_NEAR(fire.aheadBehindSeconds, plain.aheadBehindSeconds, 1e-12);
            ASSERT_TRUE(sameTone(fire.currentTone, plain.currentTone));
            ASSERT_EQ(fire.driverPage, plain.driverPage);
            return true;
        });

        // ---- beeps ----

        suite->addTest("tick: a navigation waypoint beeps once as it is reached, with its advance", []() {
            RallyState s = running();
            s.beep_assist_enabled = true;
            s.beep_navigation_mode = true;
            s.beep_advance_m = 100.0;
            s.beep_waypoints_m = { 1000.0, 2000.0 };
            TickMemory mem; TickDecision out;
            tickCopilot(s, at(500), 50000, mem, out);
            ASSERT_FALSE(mem.beepCursorsStale);
            ASSERT_EQ(mem.beepNextNavIndex, static_cast<size_t>(0));
            ASSERT_EQ(out.beeps.size(), static_cast<size_t>(0));
            tickCopilot(s, at(899), 89900, mem, out);
            ASSERT_EQ(out.beeps.size(), static_cast<size_t>(0));
            tickCopilot(s, at(900), 90000, mem, out);                // 100 m before the waypoint
            ASSERT_EQ(out.beeps.size(), static_cast<size_t>(1));
            ASSERT_TRUE(out.beeps[0].kind == TickBeep::AssistNavigation);
            ASSERT_NEAR(out.beeps[0].waypoint_m, 1000.0, 1e-9);
            ASSERT_NEAR(out.beeps[0].travelled_m, 900.0, 1e-9);
            ASSERT_EQ(mem.beepNextNavIndex, static_cast<size_t>(1));
            tickCopilot(s, at(950), 95000, mem, out);                // not again
            ASSERT_EQ(out.beeps.size(), static_cast<size_t>(0));
            tickCopilot(s, at(1900), 190000, mem, out);              // the next one
            ASSERT_EQ(out.beeps.size(), static_cast<size_t>(1));
            ASSERT_NEAR(out.beeps[0].waypoint_m, 2000.0, 1e-9);
            return true;
        });

        suite->addTest("tick: stale bookmarks are re-derived from where the car is; waypoints behind it stay silent", []() {
            RallyState s = running();
            s.beep_assist_enabled = true;
            s.beep_navigation_mode = true;
            s.beep_waypoints_m = { 1000.0, 2000.0, 3000.0 };
            TickMemory mem; TickDecision out;
            tickCopilot(s, at(2500), 250000, mem, out);              // a restart 2.5 km in
            ASSERT_EQ(mem.beepNextNavIndex, beepCursorFor(s.beep_waypoints_m, 2500.0));
            ASSERT_EQ(mem.beepNextTimingIndex,
                      beepTimingCursorFor(s.beep_waypoints_m, 250.0, s.stage_segments, s.beep_advance_s));
            ASSERT_EQ(out.beeps.size(), static_cast<size_t>(0));
            // With no stage running the timing bookmark starts at the first waypoint.
            RallyState i = idle();
            i.beep_assist_enabled = true;
            i.beep_waypoints_m = { 1000.0 };
            TickMemory m2; TickDecision o2;
            m2.beepNextTimingIndex = 7;
            tickCopilot(i, at(0), 0, m2, o2);
            ASSERT_EQ(m2.beepNextTimingIndex, static_cast<size_t>(0));
            return true;
        });

        suite->addTest("tick: timing beeps on the scheduled time; both due on one tick sound navigation first", []() {
            RallyState s = running();
            s.beep_assist_enabled = true;
            s.beep_timing_mode = true;
            s.beep_advance_s = 5.0;
            s.beep_waypoints_m = { 1000.0 };          // scheduled at 100 s
            TickMemory mem; TickDecision out;
            tickCopilot(s, at(0), 0, mem, out);
            int fired_at = -1;
            for (int t = 1; t <= 120 && fired_at < 0; t++) {
                tickCopilot(s, at(static_cast<uint64_t>(t) * 5), t * 1000, mem, out);   // crawling: only time is due
                if (!out.beeps.empty()) fired_at = t;
            }
            ASSERT_EQ(fired_at, 95);
            ASSERT_EQ(out.beeps.size(), static_cast<size_t>(1));
            ASSERT_TRUE(out.beeps[0].kind == TickBeep::AssistTiming);
            // Both modes on, both due on the same tick.
            s.beep_navigation_mode = true;
            TickMemory m2; TickDecision o2;
            tickCopilot(s, at(0), 0, m2, o2);
            tickCopilot(s, at(1000), 100000, m2, o2);
            ASSERT_EQ(o2.beeps.size(), static_cast<size_t>(2));
            ASSERT_TRUE(o2.beeps[0].kind == TickBeep::AssistNavigation);
            ASSERT_TRUE(o2.beeps[1].kind == TickBeep::AssistTiming);
            return true;
        });

        suite->addTest("tick: Beep Assist is silent when switched off, and in italian", []() {
            RallyState s = running();
            s.beep_navigation_mode = true;
            s.beep_waypoints_m = { 1000.0 };
            TickMemory mem; TickDecision out;
            tickCopilot(s, at(0), 0, mem, out);                      // switched off: bookmarks left stale
            tickCopilot(s, at(1000), 100000, mem, out);
            ASSERT_EQ(out.beeps.size(), static_cast<size_t>(0));
            ASSERT_TRUE(mem.beepCursorsStale);
            RallyState it = sector();
            it.beep_assist_enabled = true;
            it.beep_navigation_mode = true;
            it.beep_waypoints_m = { 100.0 };
            TickMemory m2; TickDecision o2;
            tickCopilot(it, at(0), 0, m2, o2);
            tickCopilot(it, at(150), 15000, m2, o2);
            for (const TickBeep& b : o2.beeps) ASSERT_TRUE(b.kind == TickBeep::Mark);
            ASSERT_TRUE(m2.beepCursorsStale);
            return true;
        });

        suite->addTest("tick, surrey: Beep Assist measures against beepDistanceM", []() {
            RallyState s = running(RALLY_SURREY);
            s.beep_assist_enabled = true;
            s.beep_navigation_mode = true;
            s.beep_waypoints_m = { 1000.0 };
            TickMemory mem; TickDecision out;
            tickCopilot(s, at(500), 50000, mem, out);
            tickCopilot(s, at(1000), 100000, mem, out);
            const double want = beepDistanceM(s, 1000, 1000, 1000.0);
            if (want >= 1000.0) {
                ASSERT_EQ(out.beeps.size(), static_cast<size_t>(1));
                ASSERT_NEAR(out.beeps[0].travelled_m, want, 1e-9);
            } else {
                ASSERT_EQ(out.beeps.size(), static_cast<size_t>(0));
            }
            return true;
        });

        suite->addTest("tick: a zero beep owed after an early next goes to the box, then the phone, once", []() {
            RallyState s = idle();
            TickMemory mem; TickDecision out;
            mem.pcZeroBeepPendingBox = true;
            mem.pcZeroBeepPendingPhone = true;
            tickCopilot(s, at(0), 0, mem, out);
            ASSERT_EQ(out.beeps.size(), static_cast<size_t>(2));
            ASSERT_TRUE(out.beeps[0].kind == TickBeep::Mark && out.beeps[0].mark == static_cast<int>(PcBeep::Zero));
            ASSERT_TRUE(out.beeps[0].sound && !out.beeps[0].announce);
            ASSERT_TRUE(out.beeps[1].mark == static_cast<int>(PcBeep::Zero));
            ASSERT_TRUE(!out.beeps[1].sound && out.beeps[1].announce);
            ASSERT_FALSE(mem.pcZeroBeepPendingBox);
            ASSERT_FALSE(mem.pcZeroBeepPendingPhone);
            tickCopilot(s, at(0), 10, mem, out);
            ASSERT_EQ(out.beeps.size(), static_cast<size_t>(0));
            return true;
        });

        suite->addTest("tick, italian: the press-warning marks are pcCountdownBeep on successive readings, each device on its lead", []() {
            RallyState s = sector();
            s.phone_beep_lead_ms = 300;
            TickMemory mem; TickDecision out;
            int box_marks = 0, phone_marks = 0;
            bool have_prev = false;
            double prev = 0.0;
            bool line_ok = false;
            const int64_t line_ms = static_cast<int64_t>(sectorSecondsToLine(s, 0, &line_ok) * 1000.0);
            ASSERT_TRUE(line_ok);
            for (int64_t t = line_ms - 8000; t <= line_ms + 2000; t += 10) {   // the last 8 s of the row, and past it
                tickCopilot(s, at(0), t, mem, out);
                bool ok = false;
                const double pc_s = pcCountdownSeconds(s, t, &ok);
                PcBeep want_box = PcBeep::None, want_phone = PcBeep::None;
                if (!ok) { have_prev = false; }
                else if (!have_prev) { have_prev = true; prev = pc_s + 1e-6; }
                else {
                    want_box = pcCountdownBeep(prev - PC_BEEP_LEAD_S, pc_s - PC_BEEP_LEAD_S);
                    want_phone = pcCountdownBeep(prev - 0.3, pc_s - 0.3);
                    prev = pc_s;
                }
                size_t i = 0;
                if (want_box != PcBeep::None) {
                    ASSERT_TRUE(i < out.beeps.size());
                    ASSERT_TRUE(out.beeps[i].mark == static_cast<int>(want_box) && out.beeps[i].sound && !out.beeps[i].announce);
                    i++; box_marks++;
                }
                if (want_phone != PcBeep::None) {
                    ASSERT_TRUE(i < out.beeps.size());
                    ASSERT_TRUE(out.beeps[i].mark == static_cast<int>(want_phone) && !out.beeps[i].sound && out.beeps[i].announce);
                    i++; phone_marks++;
                }
                ASSERT_EQ(out.beeps.size(), i);     // the car has not moved: no 100 m beep
            }
            ASSERT_TRUE(box_marks > 0);
            ASSERT_EQ(box_marks, phone_marks);
            return true;
        });

        suite->addTest("tick, italian: one beep 100 m before the end of a row that is not a pc", []() {
            RallyState s = sector();          // row 1: navi, 380 m
            TickMemory mem; TickDecision out;
            bool ok = false;
            rowMetresToLine(s, sectorPositionM(s, 270, 270), &ok);
            ASSERT_TRUE(ok);
            int beeps = 0;
            for (uint64_t c = 250; c <= 320; c++) {
                tickCopilot(s, at(c), 1000, mem, out);
                for (const TickBeep& b : out.beeps) {
                    ASSERT_TRUE(b.kind == TickBeep::Mark && b.mark == static_cast<int>(PcBeep::Tick));
                    ASSERT_TRUE(b.sound && b.announce);
                    ASSERT_EQ(c, static_cast<uint64_t>(280));       // 100 m to go
                    beeps++;
                }
            }
            ASSERT_EQ(beeps, 1);
            // A row joined already inside the last 100 m stays silent.
            TickMemory m2; TickDecision o2;
            for (uint64_t c = 300; c <= 320; c++) {
                tickCopilot(s, at(c), 1000, m2, o2);
                ASSERT_EQ(o2.beeps.size(), static_cast<size_t>(0));
            }
            return true;
        });

        suite->addTest("tickDrive takes the stage position once, as stagePositionMm gives it, running or idle", []() {
            for (RallyState s : { running(), idle() }) {
                s.total_distance_adjust_mm = -120;   // the correction is in it too
                TickMemory mem;
                TickDecision out;
                drive(s, 2345, 60000, mem, out);
                ASSERT_EQ(out.stagePositionMm, stagePositionMm(s, 2345, 2345));
            }
            return true;
        });

        return suite;
    }
};

#endif // TEST_RALLY_TICK_H
