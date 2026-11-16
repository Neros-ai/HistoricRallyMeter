#ifndef RALLY_TICK_H
#define RALLY_TICK_H

// The rally's per-tick decisions, with no widgets and no audio. update_display (the 10 ms
// tick) runs the two halves below and acts on what they decided; the driver display, the
// co-pilot display and the phone telemetry only draw / send the TickDecision.
//
// Two halves, not one call, because of what sits between them in the tick: an autostart
// that fires (or is dropped) changes the rally state, and the beeps have always been
// decided AFTER that, on a fresh reading of the rally clock.
//
//   tickDrive    everything the driver display used to work out: speed, averages,
//                ahead/behind, the driver page, the tone, the Stage arrival, and whether
//                the autostart fires / is dropped on this tick
//   (the tick sets the tone, draws the driver labels, starts or drops the autostart)
//   tickCopilot  Beep Assist and the italian press-warning beeps
//
// Every figure is produced by the same existing calculation, with the same inputs and in
// the same order, as when this lived in ui_driver.cpp / ui_copilot.cpp.

#include <cstdint>
#include <vector>
#include "simple_tone.h"
#include "arrow_tone.h"
#include "tone_cadence.h"
#include "driver_trial.h"

class RallyState;
struct CounterPoll;
enum class PcBeep;   // sector.h

// Which compact page the driver gauge draws (same numbers as AppData::DP_* and the phone's "page").
// DRIVER_PAGE_ITALIAN_TRIAL (1) and DRIVER_PAGE_ITALIAN_NAVI (2) are in driver_trial.h.
const int DRIVER_PAGE_CLASSIC = 0, DRIVER_PAGE_GREEK_STAGE = 3, DRIVER_PAGE_GREEK_IDLE = 4;

// The Stage arrival figure for the loaded stage: minutes until its start time and the speed
// that reaches the start line a minute early (stageArrival), from the distance still to go
// (metresToStageStart) and the rally clock's time of day. One calculation; who shows it is
// a display rule (driverShowsStageArrival), not a second calculation.
struct StageArrivalNow {
    bool valid = false;          // as StageArrival
    int minutes = 0;
    int speed_kph = 0;
    bool hasStartTime = false;   // a stage is loaded, it has a start time, and the clock could be read
    double minutesToStart = 0.0; // start time minus time of day, in minutes (only with hasStartTime)
};
StageArrivalNow stageArrivalNow(const RallyState& s, uint64_t c1, uint64_t c2, int64_t now_ms);

// Display rule: the driver screen shows the Stage arrival (and the last-minute stage speed)
// in greek and surrey only -- an italian sector has no distance to its start.
bool driverShowsStageArrival(const RallyState& s);

// What the tick does about a pending autostart.
enum class AutoStartTick {
    NotPending,      // nothing armed (or already fired): no countdown
    CountdownShown,  // armed, inside the T- popup window
    ArmedWaiting,    // armed, not yet inside the popup window
    Fire,            // the moment has come: the tick starts the stage (performAutoStart)
    Dropped          // the moment went by unfired: the tick drops it (performAbortStage)
};

// One beep the tick decided to sound, in the order they are sounded.
struct TickBeep {
    enum Kind { AssistNavigation, AssistTiming, Mark } kind = Mark;
    int mark = 0;              // Mark: the PcBeep (Tick, Five or Zero)
    bool sound = false;        // Mark: on the box's speaker
    bool announce = false;     // Mark: told to the phone
    double waypoint_m = 0.0;   // Beep Assist: the waypoint that fired, and the distance then
    double travelled_m = 0.0;
};

// What is carried from one tick to the next (latches and bookmarks). Runtime only, never saved.
struct TickMemory {
    // Greek stage page: where the run being driven began (the box where the speed last changed).
    long runSegIndex = -2;
    uint64_t runStartCntr1 = 0, runStartCntr2 = 0;
    int64_t runStartMs = 0;
    long runStartAdjustMm = 0;
    // Tone latches (simple_tone.h, arrow_tone.h).
    SimpleToneState simpleToneState;
    ArrowToneState arrowToneState;
    // Set on the tick an autostart fires, so it fires once. Only the arming / clearing paths clear it.
    bool autoStartTriggered = false;
    // Beep Assist: the next waypoint not yet beeped, one bookmark per mode, and "re-derive them".
    size_t beepNextNavIndex = 0;
    size_t beepNextTimingIndex = 0;
    bool beepCursorsStale = true;
    // Press-warning beeps (italian): the row being counted, the pc's tube time when it has one,
    // and the last seconds-to-line reading, so each mark is heard once.
    long pcBeepRow = -1;
    int64_t pcBeepTrialMs = -1;
    double pcBeepPrevS = 0.0;
    // next pressed within 2 s of zero: the zero beep for the row just left is still owed, to the
    // box's speaker and to the phone separately.
    bool pcZeroBeepPendingBox = false;
    bool pcZeroBeepPendingPhone = false;
    // 100 m beep on a non-pc row: the row, and the last metres-to-go reading.
    long distBeepRow = -1;
    double distBeepPrevM = 0.0;
};

// Everything the tick decided. Updated in place every tick; a field that only applies on one
// driver page keeps its last value on the others, exactly as before.
struct TickDecision {
    // ---- tickDrive ----
    int64_t nowMs = 0;                 // the rally time this half ran at
    int64_t stagePositionMm = 0;       // stagePositionMm at this half's poll (the gauge, the tone, the driver's next line)
    bool holdTimeErrorAtZero = false;  // an on-the-minute autostart counting down (or italian, no sector)
    double currentSpeed = -1.0;        // rolling speed in the box's units, -1 = none
    double smoothedSpeed = -1.0;       // the speed shown: EMA of currentSpeed, carried tick to tick
    bool earlyCountdown = false;       // an early departure counting down: no averages yet
    double driverTripAvg = 0.0, driverTotalAvg = 0.0;   // averages in the box's units, unrounded
    long totalDistM = 0, tripDistM = 0;                  // Total and Trip with their corrections, metres
    double driverTotalM = 0.0, driverTripM = 0.0;        // the driver page's Total (surrey: navi) and Trip
    long driverStageNo = 0;            // the loaded stage
    StageArrivalNow arrival;           // the Stage arrival, once per tick, for every screen
    int driverArrivalKph = -1;         // the driver screen's copy under its display rule, -1 = none
    double driverStartSpeedKph = -1;   // the last minute before the start: the stage's first speed, -1 = none
    bool stageOnGauge = false;         // a stage is running and not finished: needle, target and tone live
    double targetKph = 0.0;            // the current row's speed, kph (only with stageOnGauge)
    double aheadBehindSeconds = 0.0;
    DriverTrialView driverTrial;       // italian pc / pm layout
    int driverPage = DRIVER_PAGE_GREEK_IDLE;
    double driverRowS = 0.0;           // italian navi row: its duration at its target speed
    bool driverMinutesValid = false;
    bool driverMinutesBehind = false;  // italian navi: truly behind, even when the minutes read 0
    long driverMinutes = 0;            // italian navi: minutes ahead (+) / behind (-)
    double driverSubAvgKph = 0.0;      // greek stage page: the average over the run being driven
    bool driverSubAvgValid = false;
    double segmentProgress = 0.0;      // the needle's chevron, 0..1
    bool inSegment = false;
    ArrowToneResult arrow = {};        // the speed-adjust arrows (only with stageOnGauge)
    ToneCadence currentTone;           // the ahead/behind tone, for the speaker and the phone
    AutoStartTick autoStart = AutoStartTick::NotPending;
    int64_t autoStartRemainingMs = 0;  // how long the armed autostart still has to run

    // ---- tickCopilot ----
    int64_t copilotNowMs = 0;          // the rally time this half ran at
    std::vector<TickBeep> beeps;       // this tick's beeps, in sounding order
};

// First half. `poll` is the corrected count, `raw_poll` / `tenth_poll` the raw count now and
// ten polls back (speed), `now_ms` the rally time. `auto_start_remaining_ms` and
// `hold_time_error_at_zero` are autoStartRemaining_ms() and autoStartHoldActive(), read by
// the tick just before, as the driver display always read them. Changes no rally state: when
// it decides AutoStartTick::Fire it sets mem.autoStartTriggered, and the tick does the rest.
void tickDrive(const RallyState& s, const CounterPoll& poll, const CounterPoll& raw_poll,
               const CounterPoll& tenth_poll, int64_t now_ms,
               int64_t auto_start_remaining_ms, bool hold_time_error_at_zero,
               TickMemory& mem, TickDecision& out);

// Second half: Beep Assist (greek, surrey) and the italian press-warning / 100 m beeps, into
// out.beeps. Run after the tick has acted on the autostart, with the rally time read then.
void tickCopilot(const RallyState& s, const CounterPoll& poll, int64_t now_ms,
                 TickMemory& mem, TickDecision& out);

#endif // RALLY_TICK_H
