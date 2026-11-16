#include "rally_tick.h"
#include "calculations.h"
#include "rally_state.h"
#include "rally_types.h"
#include "sector.h"
#include "surrey.h"
#include "stage_phase.h"
#include "home_calc.h"
#include "stage_rows.h"

StageArrivalNow stageArrivalNow(const RallyState& s, uint64_t c1, uint64_t c2, int64_t now_ms) {
    StageArrivalNow a;
    const int slot = s.last_memory_slot;
    if (slot < 1 || slot > RallyState::MAX_MEMORY_SLOTS) return a;
    const long start_s = s.memory_slots[slot - 1].start_time_s;
    const long start_dist = s.memory_slots[slot - 1].start_distance_m;
    if (start_s < 0) return a;
    long tod = 0;
    if (!timeOfDaySeconds(now_ms, &tod)) return a;
    a.hasStartTime = true;
    a.minutesToStart = (start_s - tod) / 60.0;
    // Distance still to go to the start line (Total as shown); past it, nothing.
    const StageArrival arr = stageArrival(
        static_cast<double>(metresToStageStart(s, c1, c2, start_dist)), a.minutesToStart);
    a.valid = arr.valid;
    a.minutes = arr.minutes;
    a.speed_kph = arr.speed_kph;
    return a;
}

bool driverShowsStageArrival(const RallyState& s) {
    // Distance to the start belongs to greek and surrey only; an italian sector has none.
    return !isItalian(s);
}

void tickDrive(const RallyState& s, const CounterPoll& current_poll, const CounterPoll& current_raw_poll,
               const CounterPoll& tenth_poll, int64_t current_time_ms,
               int64_t auto_start_remaining_ms, bool holdTimeErrorAtZero,
               TickMemory& mem, TickDecision& out) {
    out.nowMs = current_time_ms;
    // How long an armed autostart still has to run, and whether the time error holds at zero
    // while an "on the minute" autostart counts down (see autoStartHoldsTimeError).
    out.autoStartRemainingMs = auto_start_remaining_ms;
    out.holdTimeErrorAtZero = holdTimeErrorAtZero;
    // The car's position from the stage start, once for this half (the state is not changed here).
    out.stagePositionMm = stagePositionMm(s, current_poll.cntr1, current_poll.cntr2);

    // Current speed (from rolling average, then EMA-smoothed for display).
    // Speed is measured on the raw count so it stays positive in reverse.
    const double current_speed = calculateCurrentSpeed(s, current_raw_poll, tenth_poll);
    out.currentSpeed = current_speed;
    if (current_speed < 0) {
        out.smoothedSpeed = -1.0;
    } else if (out.smoothedSpeed < 0) {
        out.smoothedSpeed = current_speed;
    } else {
        constexpr double alpha = 0.02;
        out.smoothedSpeed = alpha * current_speed + (1.0 - alpha) * out.smoothedSpeed;
    }

    // Trip average speed
    const int64_t trip_count_diff = calculateDistanceCounts(s,
        current_poll.cntr1, current_poll.cntr2,
        s.trip_start_cntr1, s.trip_start_cntr2);
    double trip_speed = calculateAverageSpeed(s,
        s.trip_start_time_ms, current_time_ms, trip_count_diff,
        s.trip_distance_adjust_mm);
    trip_speed = averageSpeedForDisplay(trip_speed, holdTimeErrorAtZero);
    out.driverTripAvg = trip_speed;
    out.earlyCountdown = stagePhase(s) == StagePhase::EarlyCountdown;

    // Total average speed
    const int64_t total_count_diff = calculateDistanceCounts(s,
        current_poll.cntr1, current_poll.cntr2,
        s.total_start_cntr1, s.total_start_cntr2);
    double total_speed = calculateAverageSpeed(s,
        s.total_start_time_ms, current_time_ms, total_count_diff,
        s.total_distance_adjust_mm);
    total_speed = averageSpeedForDisplay(total_speed, holdTimeErrorAtZero);
    out.driverTotalAvg = total_speed;

    // Total/Trip distance, reusing the count differences just computed for
    // the average speeds above -- no additional counter reads. Applies the
    // same manual correction the co-pilot applies (RB-NAV-03).
    const long total_dist_m = adjustedDistanceMetersMm(
        countsToMm(total_count_diff, s.calibration),
        s.total_distance_adjust_mm);
    out.totalDistM = total_dist_m;
    const long trip_dist_m = adjustedDistanceMetersMm(
        countsToMm(trip_count_diff, s.calibration),
        s.trip_distance_adjust_mm);
    out.tripDistM = trip_dist_m;
    // Surrey: Total is the roadbook's running distance, as on the co-pilot page.
    out.driverTotalM = isSurrey(s)
        ? static_cast<double>(surreyNaviMetres(s, current_poll.cntr1, current_poll.cntr2))
        : static_cast<double>(total_dist_m);
    out.driverTripM  = static_cast<double>(trip_dist_m);
    out.driverStageNo = s.last_memory_slot;

    // Stage arrival, once for every screen. The driver screen takes the speed to reach the
    // loaded stage's start with a minute spare (as Home), under its own display rule.
    out.arrival = stageArrivalNow(s, current_poll.cntr1, current_poll.cntr2, current_time_ms);
    out.driverArrivalKph = -1;
    out.driverStartSpeedKph = -1;
    if (driverShowsStageArrival(s) && out.arrival.hasStartTime) {
        if (out.arrival.valid) out.driverArrivalKph = out.arrival.speed_kph;
        const double mins_to_start = out.arrival.minutesToStart;
        // The coming stage is the loaded roadbook (a start adopts it), not the last stage driven.
        if (mins_to_start > 0.0 && mins_to_start <= 1.0 && !s.segments.empty())
            out.driverStartSpeedKph = s.segments[0].target_speed_kph;
    }

    if (s.segment_current_number < 0) mem.runSegIndex = -2;   // no stage: the next one starts a new run
    // Target speed and ahead/behind. A finished stage is no longer on the gauge: the display
    // goes to the navigation page (no needle, no tone) as it does after an abort, in every rally
    // type. A surrey stage reopened by prev comes back here.
    if (s.segment_current_number >= 0 && !s.stage_complete &&
        s.segment_current_number < static_cast<long>(s.stage_segments.size())) {
        out.stageOnGauge = true;
        const Segment& seg = s.stage_segments[s.segment_current_number];
        out.targetKph = seg.target_speed_kph;

        // Ahead/behind - calculated from stage start accounting for all segment speeds
        // Corrected, not raw: a wrong turn knocked off with -10 has to move
        // the time error too, or the gauge keeps crediting distance the crew
        // just told the box they had not driven.
        const int64_t stage_now_mm = out.stagePositionMm;
        // Italian: the pc / pm error inside one, at rest (tone silent) on navi / ct / co subs.
        double seconds = gaugeAheadSeconds(s, current_poll.cntr1, current_poll.cntr2,
                                           current_time_ms, stage_now_mm);
        // Nothing to be ahead or behind of until the clock zeroes at the
        // minute. Held before the value is stored, so the gauge needle, the
        // chevrons and the tone all sit at rest too rather than tracking a
        // stage that is not running yet.
        if (holdTimeErrorAtZero) seconds = 0.0;
        out.aheadBehindSeconds = seconds;

        // RB-DRV-18: inside an italian pc / pm the compact gauge shows its own layout
        // (sector total, pc / pm average, logo, 5 s countdown), every figure in km and km/h.
        if (holdTimeErrorAtZero) {
            out.driverTrial = DriverTrialView{};
        } else {
            out.driverTrial = driverTrialView(s, current_poll.cntr1, current_poll.cntr2,
                                              current_time_ms);
        }
        {
            const bool italian = isItalian(s) && stageIsSector(s.stage_segments);
            out.driverPage = out.driverTrial.active ? DRIVER_PAGE_ITALIAN_TRIAL
                              : (italian ? DRIVER_PAGE_ITALIAN_NAVI : DRIVER_PAGE_GREEK_STAGE);
            if (out.driverPage == DRIVER_PAGE_ITALIAN_NAVI) {
                out.driverRowS = rowSeconds(seg);
                bool v_ok = false;
                const double ahead = sectorAheadSeconds(s, current_poll.cntr1, current_poll.cntr2,
                                                        current_time_ms, &v_ok);
                out.driverMinutesValid = v_ok;
                const long m = minutesInHand(ahead);
                out.driverMinutes = ahead >= 0.0 ? m : -m;
                out.driverMinutesBehind = ahead < 0.0;
            }
        }

        // Fraction of the current segment driven, for the needle's scale chevrons
        // and segment-end tick. "In segment" while between the segment start and end.
        {
            // Driven in this row: the stage position minus where the row starts (stage start, typed metres summed).
            const int64_t seg_driven_mm = stage_now_mm - segmentStartStageMm(s.stage_segments,
                                                                               s.segment_current_number);
            {   // average speed over the run being driven: from the box where the speed last changed.
                // The run's start is taken when the segment changes: kept through a box that carries
                // the speed on, restarted at a speed change, the first box, or any step back.
                const long cur_seg = s.segment_current_number;
                if (cur_seg != mem.runSegIndex) {
                    const bool carried = cur_seg == mem.runSegIndex + 1 && cur_seg > 0 &&
                        !s.stage_segments[static_cast<size_t>(cur_seg)].changesSpeed;
                    if (!carried) {
                        mem.runStartCntr1 = s.segment_start_cntr1;
                        mem.runStartCntr2 = s.segment_start_cntr2;
                        mem.runStartMs = s.segment_start_time_ms;
                        mem.runStartAdjustMm = s.segment_start_adjust_mm;
                    }
                    mem.runSegIndex = cur_seg;
                }
                // The original average function, fed the run's own start.
                const int64_t run_counts = calculateDistanceCounts(s, current_poll.cntr1, current_poll.cntr2,
                                                                   mem.runStartCntr1, mem.runStartCntr2);
                const double avg = calculateAverageSpeed(s, mem.runStartMs, current_time_ms,
                                                         run_counts, s.total_distance_adjust_mm - mem.runStartAdjustMm);
                out.driverSubAvgValid = (current_time_ms - mem.runStartMs > 500 && avg > 0.0);
                out.driverSubAvgKph = out.driverSubAvgValid ? (s.units ? mphToKph(avg) : avg) : 0.0;
            }
            const double seg_total = seg.distance_m * 1000.0;   // millimetres
            const double seg_driven = static_cast<double>(seg_driven_mm);
            if (seg_total > 0.0) {
                double frac = seg_driven / seg_total;
                if (frac < 0.0) frac = 0.0;
                if (frac > 1.0) frac = 1.0;
                out.segmentProgress = frac;
                out.inSegment = (seg_driven >= 0 && seg_driven <= seg_total);
            } else {
                out.segmentProgress = 0.0;
                out.inSegment = false;
            }
            if (!isItalian(s)) {
                // Greek and Surrey: the chevron works on TIME, the only thing known for sure. It walks the
                // scheduled row the stage clock is in, from its scheduled start to its scheduled end.
                bool in_row = false;
                const double stage_s = (current_time_ms - s.total_start_time_ms) / 1000.0;
                out.segmentProgress = scheduledRowProgress(s.stage_segments, stage_s, &in_row);
                out.inSegment = in_row;
            }
            if (out.driverPage == DRIVER_PAGE_ITALIAN_NAVI) {
                // Italian navi row: the chevron follows the countdown seconds, as in a pc / pm -- waits at the
                // start, reaches the end tick over the last 5 s, on the bar as the countdown reads 0.
                bool sec_ok = false;
                const double to_line = sectorSecondsToLine(s, current_time_ms, &sec_ok);
                if (sec_ok) {
                    out.segmentProgress = finalWindowProgress(to_line);
                    out.inSegment = true;
                } else {
                    out.segmentProgress = 0.0;
                    out.inSegment = false;
                }
            }
        }

        // Speed adjustment arrows (always driven by the arrow-based
        // calculation, regardless of tone mode -- the visual indicator is
        // unaffected by the tone types) and the ahead/behind tone
        // (driven by whichever algorithm the type for the current side selects).
        double stage_dist_m = static_cast<double>(stage_now_mm) / 1000.0;
        // Italian: the tone runs from the moment a pc (its trial running) or a pm starts, not 250 m into the sector.
        if (isItalian(s) && stage_dist_m < 250.0 &&
            (seg.type == SEG_PM || (seg.type == SEG_PC && s.trial_start_m >= 0.0)))
            stage_dist_m = 250.0;
        const bool past_stage_end = stageDistanceComplete(s.stage_segments, stage_now_mm);

        out.arrow = computeArrowBasedTone(
            mem.arrowToneState,
            seconds, seg.target_speed_kph,
            s.units, stage_dist_m, past_stage_end);

        // One decision for the box's speaker and the phone (RB-WEB-02).
        out.currentTone = decideToneCadence(
            s.tone_enabled,
            simpleToneForError(seconds, s.tone_type_behind, s.tone_type_ahead),
            out.arrow,
            mem.simpleToneState, seconds, stage_dist_m, past_stage_end);
    } else {
        out.stageOnGauge = false;
        out.currentTone = ToneCadence{};   // silent, on the speaker and the phone
        out.aheadBehindSeconds = 0.0;
        out.segmentProgress = 0.0;
        out.inSegment = false;
        out.driverTrial = DriverTrialView{};
        out.driverPage = DRIVER_PAGE_GREEK_IDLE;
    }

    // The pending autostart.
    if (s.auto_start_rally_time_s > 0 && !mem.autoStartTriggered) {
        const int64_t diff_ms = auto_start_remaining_ms;
        if (autoStartCountdownVisible(s.auto_start_early_departure, diff_ms)) {
            out.autoStart = AutoStartTick::CountdownShown;
        } else if (diff_ms > 0) {
            // Armed, but not yet inside the popup window: the clock runs, no popup.
            out.autoStart = AutoStartTick::ArmedWaiting;
        } else if (diff_ms <= 0 && diff_ms > -AUTO_START_TRIGGER_WINDOW_MS) {
            mem.autoStartTriggered = true;
            out.autoStart = AutoStartTick::Fire;
        } else {
            // The armed time has gone by without firing (the rally clock was set forward past it,
            // or the display stalled through the trigger window). As at startup: dropped, idle as
            // after an abort, never a late start -- the crew press stage go.
            out.autoStart = AutoStartTick::Dropped;
        }
    } else {
        out.autoStart = AutoStartTick::NotPending;
    }
}

void tickCopilot(const RallyState& s, const CounterPoll& current_poll, int64_t current_time_ms,
                 TickMemory& mem, TickDecision& out) {
    out.copilotNowMs = current_time_ms;
    out.beeps.clear();

    const int64_t total_count_diff = calculateDistanceCounts(s,
        current_poll.cntr1, current_poll.cntr2,
        s.total_start_cntr1, s.total_start_cntr2);

    // Beep Assist. Decided on every tick, whatever screen the operator is on. Greek and surrey
    // only: italian has no navigation beeps (owner 2026-10-30), only the pc countdown below.
    if (!isItalian(s) &&
        s.beep_assist_enabled && !s.beep_waypoints_m.empty()) {
        // The corrected Total, not the raw counter: both readouts apply
        // total_distance_adjust_mm (RB-NAV-03), and a waypoint is a roadbook
        // distance the operator compares against what the display shows.
        double travelled_m = static_cast<double>(adjustedDistanceMetersMm(
            countsToMm(total_count_diff, s.calibration),
            s.total_distance_adjust_mm));
        travelled_m = beepDistanceM(s, current_poll.cntr1, current_poll.cntr2, travelled_m);   // surrey: navi
        const bool stage_active = s.segment_current_number >= 0;
        const double elapsed_stage_s = (current_time_ms - s.total_start_time_ms) / 1000.0;

        // Re-derive the cursors whenever they have been marked stale: at
        // startup, after an edit to the list, and when Beep Assist or one of
        // its modes is switched on. Done here rather than at the point the
        // flag is set because this is the first place the counter is known to
        // have been polled.
        if (mem.beepCursorsStale) {
            mem.beepNextNavIndex = beepCursorFor(s.beep_waypoints_m, travelled_m);
            // Timing mode is ordered by scheduled time, not distance: a car
            // running ahead of schedule has reached waypoints whose roadbook
            // time has not arrived, and a distance-derived cursor would step
            // past them and drop their beeps.
            mem.beepNextTimingIndex = stage_active
                ? beepTimingCursorFor(s.beep_waypoints_m, elapsed_stage_s,
                                      s.stage_segments, s.beep_advance_s)
                : 0;
            mem.beepCursorsStale = false;
        }

        // Navigation and timing are independent warnings, each checked
        // against its own cursor, so a tick where both are due fires both.
        const long nav_due = dueBeepWaypoint(s.beep_waypoints_m, mem.beepNextNavIndex,
                                             travelled_m,
                                             s.beep_navigation_mode, s.beep_advance_m,
                                             false, 0.0,
                                             stage_active, elapsed_stage_s, s.stage_segments);
        if (nav_due >= 0) {
            TickBeep b;
            b.kind = TickBeep::AssistNavigation;
            b.waypoint_m = s.beep_waypoints_m[static_cast<size_t>(nav_due)];
            b.travelled_m = travelled_m;
            out.beeps.push_back(b);
            mem.beepNextNavIndex = static_cast<size_t>(nav_due) + 1;
        }

        const long timing_due = dueBeepWaypoint(s.beep_waypoints_m, mem.beepNextTimingIndex,
                                                travelled_m,
                                                false, 0.0,
                                                s.beep_timing_mode, s.beep_advance_s,
                                                stage_active, elapsed_stage_s, s.stage_segments);
        if (timing_due >= 0) {
            TickBeep b;
            b.kind = TickBeep::AssistTiming;
            b.waypoint_m = s.beep_waypoints_m[static_cast<size_t>(timing_due)];
            b.travelled_m = travelled_m;
            out.beeps.push_back(b);
            mem.beepNextTimingIndex = static_cast<size_t>(timing_due) + 1;
        }
    }

    // Press warning (italian). A pc uses its own tube clock; a navi row uses the
    // sector clock, to warn that next is due. The navigation double
    // beep at 5 s, a single beep at 4, 3, 2 and 1, and a shorter beep on 0.
    // A new row starts from its present reading, so marks already gone are skipped.
    // sound: the box's speaker. announce: the phone is told (it sounds the beep on receipt).
    auto mark = [&out](PcBeep beep, bool sound, bool announce) {
        TickBeep b;
        b.kind = TickBeep::Mark;
        b.mark = static_cast<int>(beep);
        b.sound = sound;
        b.announce = announce;
        out.beeps.push_back(b);
    };
    {
        // next pressed within 2 s of the zero: the zero beep for the row just left, on each device that has not had it
        if (mem.pcZeroBeepPendingBox) { mem.pcZeroBeepPendingBox = false; mark(PcBeep::Zero, true, false); }
        if (mem.pcZeroBeepPendingPhone) { mem.pcZeroBeepPendingPhone = false; mark(PcBeep::Zero, false, true); }
        bool pc_ok = false;
        const double pc_s = isItalian(s)
            ? pcCountdownSeconds(s, current_time_ms, &pc_ok) : 0.0;
        const long row = s.segment_current_number;
        if (!pc_ok) {
            mem.pcBeepRow = -1;
        } else if (mem.pcBeepRow != row || mem.pcBeepTrialMs != s.trial_start_ms) {
            mem.pcBeepRow = row;
            mem.pcBeepTrialMs = s.trial_start_ms;
            mem.pcBeepPrevS = pc_s + 1e-6;   // a row starting exactly on a mark still hears it
        } else {
            // The box's speaker and the phone each get their own lead, so each is heard on the second.
            const PcBeep beep = pcCountdownBeep(mem.pcBeepPrevS - PC_BEEP_LEAD_S, pc_s - PC_BEEP_LEAD_S);
            const double phone_lead_s = s.phone_beep_lead_ms / 1000.0;
            const PcBeep phone_beep = pcCountdownBeep(mem.pcBeepPrevS - phone_lead_s, pc_s - phone_lead_s);
            mem.pcBeepPrevS = pc_s;
            if (beep != PcBeep::None) mark(beep, true, false);
            if (phone_beep != PcBeep::None) mark(phone_beep, false, true);
        }

        // 100 m before the end of every row that is not a pc: one single beep.
        bool dist_ok = false;
        const double left_m = rowMetresToLine(
            s,
            sectorPositionM(s, current_poll.cntr1, current_poll.cntr2),
            &dist_ok);
        if (!dist_ok) {
            mem.distBeepRow = -1;
        } else if (mem.distBeepRow != row) {
            mem.distBeepRow = row;
            mem.distBeepPrevM = left_m + 1e-3;   // already inside 100 m: stay silent
        } else {
            if (hundredMetreBeep(mem.distBeepPrevM, left_m)) mark(PcBeep::Tick, true, true);
            mem.distBeepPrevM = left_m;
        }
    }
}
