#ifndef CALCULATIONS_H
#define CALCULATIONS_H

#include <cstdint>
#include <string>
#include <vector>
#include "rally_state.h"
#include "rally_types.h"

// Calculate distance in counts
int64_t calculateDistanceCounts(const RallyState& state, uint64_t cntr1, uint64_t cntr2,
                                  uint64_t start1, uint64_t start2);

// Convert counts to meters using calibration (high precision)
double countsToMeters(int64_t counts, long calibration);

// Convert counts to centimeters using calibration
long countsToCentimeters(int64_t counts, long calibration);

// Convert counts per hour to KPH (high precision)
double countsPerHourToKPH(double counts_per_hour, long calibration);

// Convert KPH to counts per hour (high precision)
double kphToCountsPerHour(double kph, long calibration);

// Get rally time (system time + offset)
int64_t getRallyTime_ms(const RallyState& state);

// Format time as HH:MM:SS
std::string formatTime(int64_t time_ms);

// Format duration as HH:MM:SS
std::string formatDuration(int64_t duration_ms);

// Calculate current speed from 10-second rolling average
double calculateCurrentSpeed(const RallyState& state, const CounterPoll& current, 
                            const CounterPoll& tenth);

// Calculate average speed
double calculateAverageSpeed(const RallyState& state, int64_t start_time_ms, 
                            int64_t current_time_ms, int64_t count_diff);

// Calculate seconds ahead/behind target (high precision) - single segment
double calculateAheadBehind(const RallyState& state, int64_t current_time_ms,
                          int64_t segment_start_time, double target_counts_per_hour,
                          int64_t actual_counts);

// Calculate ideal counts from stage start accounting for all segment speeds
double calculateIdealCountsFromStageStart(const RallyState& state, int64_t elapsed_ms);

// Calculate seconds ahead/behind from stage start (accounts for all segments)
double calculateAheadBehindFromStageStart(const RallyState& state, int64_t current_time_ms,
                                          int64_t actual_counts_from_stage_start);

// Format a distance in meters with thousands-separator commas. No unit
// suffix and no padding -- callers that need column alignment use GTK
// label width/xalign, not string padding.
std::string formatDistanceGrouped(long meters);

// Format a distance choosing meters or kilometers so the printed magnitude
// stays bounded (switches to km above +/-999,999 m). Writes "m" or "km"
// through *unit_out.
std::string formatDistanceAutoUnit(long meters, const char** unit_out);

// Format an elapsed interval as minutes:seconds, switching to hours:minutes
// once the minutes need more than four digits, and to "toolong" when even
// hours will not fit. Emits no padding -- callers align the column with GTK
// label width and xalign, so the result does not depend on which monospace
// font the platform resolves.
std::string formatElapsedInterval(int64_t total_secs);

// Limit a proposed manual distance correction so the corrected reading can
// never fall below zero. Clamping the correction rather than the displayed
// value matters: otherwise repeated downward presses bank an invisible debt
// that silently swallows the next stretch of real travel.
long clampDistanceAdjust(long raw_cm, long proposed_adjust_cm);

// Apply a manual distance correction (centimetres) to a raw distance
// (centimetres) and return whole metres, truncating the same way the
// uncorrected path does.
long adjustedDistanceMeters(long raw_cm, long adjust_cm);
// Summary of the loaded stage for the Stage Go confirmation, built directly
// from the segments the operator has entered or recalled -- the app tracks
// no separate stage name or number, so this is the only truthful thing to
// show. Formatting matches refreshSegmentList()'s own display exactly
// (whole metres, speed to two decimal places), so the dialog can never show
// a number the operator does not already recognise from the segments page.
std::string stageSummary(const std::vector<Segment>& segments);

// True once the car has covered the whole of `segs` -- the stage's own
// distance, summed from its segments, against the counts driven since the
// stage started. The point at which the stage is over and its roadbook is no
// longer the one the crew are working to.
bool stageDistanceComplete(const std::vector<Segment>& segs, int64_t stage_counts);

// ---- Read-only stage panel ------------------------------------------------

// Caption colour for the status panel -- a label as against a value.
constexpr const char* STAGE_STATUS_CAPTION_COLOR = "#FFA500";

// Segment rows the panel has room for beneath the distance-adjust buttons on
// a 1280x400 co-pilot display, at 22px monospace (see the .stage-status rule
// in ui_copilot.cpp -- keep the two in step). Worst case is nine lines: the
// autostart line, the heading, and seven segments under the "one over the
// limit is shown rather than summarised" rule.
constexpr size_t STAGE_STATUS_MAX_ROWS = 6;

// The armed autostart as the panel shows it: "none" when nothing is armed,
// otherwise the wall-clock time it fires. Says nothing about which kind it
// is -- the driver panel's countdown already carries that -- but keeps
// `early_departure` in the signature so a caller cannot silently start
// passing the wrong thing if it comes back.
std::string formatAutoStartStatus(uint64_t auto_start_rally_time_s,
                                  bool early_departure, int64_t epoch_ms);

// The read-only stage panel on the main co-pilot screen: when the autostart
// fires, then one line per segment -- target speed, that segment's length,
// and the distance from the stage start to the end of it. The cumulative
// column is what the crew read against the odometer, since the roadbook
// gives each segment's own length but the box counts from the start.
//
// Returned as one monospace block of PANGO MARKUP (captions coloured, values
// plain) rather than built as a widget grid, so the whole layout is a pure
// function and testable without GTK -- the caller must render it with
// gtk_label_set_markup, not set_text. `max_rows` caps the segment lines to
// what the panel has room for; the rest are summarised.
std::string formatStageStatusTable(const std::vector<Segment>& segs, bool units,
                                   const std::string& autostart_text,
                                   size_t max_rows);

// ---- Autostart -------------------------------------------------------------

// The stored autostart target, converted to and from a wall-clock instant.
// Stored as whole seconds from a fixed epoch, so a target 400ms past the
// second lands on that second rather than the next one.
uint64_t autoStartSecondsFromTargetMs(int64_t target_ms, int64_t epoch_ms);
int64_t autoStartTargetMsFromSeconds(uint64_t seconds, int64_t epoch_ms);

// The complete armed-autostart state. Every arming produces one of these and
// the caller ASSIGNS it wholesale, which is what makes a press overrule
// whatever was armed before -- including an autostart of the other kind, and
// including one already armed for the very same target time. No field
// survives from the previous arming, so there is no combination of old kind
// and new time that can leave the box half-converted.
struct AutoStartArming {
    uint64_t rally_time_s;      // 0 = nothing armed
    bool early_departure;       // true = the Stage Go dialog's minute button
    bool triggered;             // always false: a fresh arming has not fired
    bool zero_distance_now;     // early departure zeroes distance at arming
};

// Builds the arming for a press. `target_ms` is the moment the stage clock
// zeroes; `early_departure` selects the kind that also zeroes the distance
// immediately, so the run to the line counts toward the stage.
AutoStartArming autoStartArming(int64_t target_ms, int64_t epoch_ms,
                                bool early_departure);

// The arming that cancels whatever is pending -- used by "Clear", and by a
// manual Stage Go, which must not leave an autostart behind to re-zero the
// stage a minute after the crew started it by hand.
AutoStartArming autoStartDisarmed();

// True while an armed early-departure autostart is still counting down, in
// which case the stage readouts sit at zero rather than reporting a figure.
//
// That autostart zeroes the DISTANCE when it is armed but deliberately
// leaves the clock running to the appointed minute, and it does not end the
// stage that is loaded. Without this the error box therefore kept reporting
// the previous stage against its old clock zero -- and since the car is
// standing still on nought distance, that figure ran a second further behind
// for every second of the countdown. There is no schedule to be ahead or
// behind of until the clock zeroes, so zero is the honest reading.
//
// Not applied to a target set on the Set Autostart screen: that one arms
// nothing and zeroes nothing until it fires, so a stage already under way
// keeps its real figures. `diff_ms` is the countdown remaining, so the hold
// lasts exactly as long as the T- overlay is on screen and a stale target
// left in a config file releases it at once.
bool autoStartHoldsTimeError(uint64_t auto_start_rally_time_s,
                             bool auto_start_early_departure,
                             bool auto_start_triggered,
                             int64_t diff_ms);

// The average speed to SHOW. The distance baselines are zeroed when an early
// departure is armed while the clock keeps running, so an average taken over
// the previous stage's clock against a distance that has just been re-zeroed
// reads as road speed the moment the car rolls -- a car creeping to the line
// showing an average it has not driven. Zero matches the time-error box
// beside it.
double averageSpeedForDisplay(double average_speed, bool hold_at_zero);

#endif // CALCULATIONS_H
