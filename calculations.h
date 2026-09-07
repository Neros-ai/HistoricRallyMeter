#ifndef CALCULATIONS_H
#define CALCULATIONS_H

#include <cstdint>
#include <string>
#include <vector>
#include <utility>
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
// Average speed over a window. adjust_cm is the manual -10/+10 correction for
// the counter in question (total_distance_adjust_cm / trip_distance_adjust_cm)
// and is applied exactly as adjustedDistanceMeters applies it to the readout:
// without it, a correction moved the displayed distance while the average
// speed carried on using the uncorrected distance, so the two numbers on the
// same panel disagreed for the rest of the stage.
double calculateAverageSpeed(const RallyState& state, int64_t start_time_ms, 
                            int64_t current_time_ms, int64_t count_diff,
                            long adjust_cm = 0);

// Target speed of the segment AFTER the current one, in KPH. Returns false --
// leaving *kph untouched -- outside a stage or on the last segment, i.e.
// whenever there is no coming change to announce. Shared by the co-pilot's
// "next" row and the driver's smaller speed under Target so the two panels
// cannot disagree about what is coming.
bool nextSegmentTargetKph(const RallyState& state, double* kph);

// Calculate seconds ahead/behind target (high precision) - single segment
double calculateAheadBehind(const RallyState& state, int64_t current_time_ms,
                          int64_t segment_start_time, double target_counts_per_hour,
                          int64_t actual_counts);

// Calculate ideal counts from stage start accounting for all segment speeds.
//
// A segment with a real distance but no target speed cannot be divided by, so
// it is skipped -- which silently drops its DISTANCE as well as its time and
// leaves the result measured against a roadbook shorter than the real one.
// Pass roadbook_complete to find out whether that happened: it is set false
// if any segment up to and including the current one had distance but no
// speed, in which case the returned figure is not trustworthy. Left null by
// callers that do not care.
double calculateIdealCountsFromStageStart(const RallyState& state, int64_t elapsed_ms,
                                          bool* roadbook_complete = nullptr);

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

// Heading for the co-pilot's next-segment row: the current segment's own
// target speed, with an arrow toward the change ahead. This is the one fact
// about the current segment the app can state honestly -- there is no
// reliable segment or stage number to show instead. Falls back to a
// placeholder when no segment is running. current_speed is already in the
// active display unit -- this function does not convert.
std::string segmentRowHeading(double current_speed, bool has_current_segment);

// Speed cell for that row: the speed coming up. Does not repeat the current
// speed -- that is already the row's heading -- so this answers "and then
// what", not "from what". Whole numbers -- roadbook speeds are called as
// whole figures. next_speed is already in the active display unit; is_mph
// only selects which unit word follows it.
std::string segmentSpeedTransition(double next_speed, bool has_next, bool is_mph);

// Parse an operator-entered Beep Assist waypoint list. Values are kilometres
// separated by commas, semicolons or whitespace, and are returned as metres
// in ascending order with duplicates removed. A malformed token is skipped
// rather than aborting the parse: one typo must not cost the whole list.
std::vector<double> parseBeepWaypointsKm(const std::string& text);

// Render waypoint metres back to the kilometre display form.
std::string formatBeepWaypointsKm(const std::vector<double>& waypoints_m);

// Index of the first waypoint not yet reached at the given travelled
// distance. Used to re-derive the runtime cursor after a restart, so a power
// blip mid-stage does not replay every waypoint already behind the car.
size_t beepCursorFor(const std::vector<double>& waypoints_m, double travelled_m);

// Ideal elapsed time, in seconds, to reach a target distance from the stage
// start, walking segments in order at each one's own target speed. Uses each
// segment's calibration-independent distance_m/target_speed_kph -- distance
// actually travelled never enters this calculation, only the roadbook's own
// numbers do. Returns -1 if the target lies beyond the distance the loaded
// segments cover, or if reaching it would require passing a segment that has
// a distance but no target speed (an incomplete roadbook: the time is
// genuinely unknown, and assuming otherwise fires later beeps early).
double idealSecondsToReachDistance(const std::vector<Segment>& segments, double target_distance_m);
// Index of the first waypoint whose scheduled (roadbook) time has not yet
// arrived. The timing-mode counterpart to beepCursorFor: timing beeps are
// ordered by TIME, so deriving their cursor from distance travelled steps
// past waypoints the car has reached early and drops their beeps.
size_t beepTimingCursorFor(const std::vector<double>& waypoints_m, double elapsed_stage_s,
                           const std::vector<Segment>& segments, double advance_s);

// Navigation mode due-check: true once the Total distance is within
// advance_m of the waypoint. Pure distance -- current speed plays no part.
bool navigationBeepDue(double waypoint_m, double travelled_m, double advance_m);

// Timing mode due-check: true once the scheduled time to reach the waypoint,
// less advance_s, has elapsed. Meaningless outside a stage; callers gate on
// that themselves (this function has no access to segment_current_number).
bool timingBeepDue(double waypoint_m, double elapsed_stage_s,
                   const std::vector<Segment>& segments, double advance_s);

// Index of the first waypoint due under either active mode, or -1 if none is
// due. Navigation and timing are independent triggers on the same waypoint
// list -- either firing is enough. The caller runs this once per mode, each
// against its own cursor (see AppData::beepNextNavIndex/beepNextTimingIndex),
// so navigation and timing can each fire independently for the same
// waypoint rather than one consuming the other's beep.
long dueBeepWaypoint(const std::vector<double>& waypoints_m, size_t from_index,
                     double travelled_m,
                     bool navigation_mode, double advance_m,
                     bool timing_mode, double advance_s,
                     bool stage_active, double elapsed_stage_s,
                     const std::vector<Segment>& segments);

// Counts per kilometre implied by a calibration value (millimetres per 1000
// counts). This is the figure the operator compares against a known-good
// number, so the screen shows it directly rather than leaving it to be
// derived from the raw calibration. Returns 0 for a non-positive calibration
// rather than dividing by zero.
double pulsesPerKm(long calibration);

// The calibration screen's readout line: distance covered, the averaged pulse
// count, and each sensor's own count -- so one wheel sensor disagreeing with
// the other is visible. Pulses/KM is shown separately, in the "Current
// Calibration" row.
std::string calibrationReadoutLine(long distance_m, int64_t counts_avg,
                                   int64_t counts_s1, int64_t counts_s2);

// The calibration value (millimetres per 1000 counts) that gives an exact
// pulses/km figure. Identical arithmetic to pulsesPerKm() -- calibration and
// pulses/km are reciprocals of each other through the same 1e9 constant --
// named separately so each call site reads for what it means. Returns 0 for
// a non-positive input rather than dividing by zero.
long calibrationFromPulsesPerKm(double pulses_per_km);

// Summary of the loaded stage for the Stage Go confirmation, built directly
// from the segments the operator has entered or recalled -- the app tracks
// no separate stage name or number, so this is the only truthful thing to
// show. Formatting matches refreshSegmentList()'s own display exactly
// (whole metres, speed to two decimal places), so the dialog can never show
// a number the operator does not already recognise from the segments page.
std::string stageSummary(const std::vector<Segment>& segments);

// Geometry of the compact (800x480-style) driver gauge layout. Pure
// arithmetic with no GTK or Cairo dependency, so the numbers can be
// unit-tested with no display. The same computation is mirrored in
// tools/layout-preview/DriverWindowTextLayout.html -- edit both together.
struct CompactGaugeLayout {
    double radius;          // arc radius
    double centerX;         // hub x
    double centerY;         // hub y
    double fscale;          // font scale, 1.0 at the reference radius
    double valSize;         // Total/Trip speed+distance value font size AND
                             // the ahead/behind digital box's font size
                             // (RB-DRV-08 dropped the box back to this size)
    double curTopSize;      // Current/Target value font size (RB-DRV-08) --
                             // the single largest text size in the layout
    double labelSize;       // row caption font size
    double labelGap;        // gap between a value's anchor and its caption
    double rowGap;          // vertical spacing between value rows
    double rightAnchor;     // right edge every Total/Trip speed value aligns to
    double distanceAnchor;  // right edge every distance value AND the
                             // "Distance (metres)" caption (RB-DRV-02) align
                             // to -- both right-aligned to the same X, so a
                             // value's last digit and the caption's closing
                             // ")" always share one vertical edge. Shifted
                             // 10px right of the arc-derived offset by
                             // RB-DRV-08.
    double bandOuterX;      // right edge of the coloured band arc's bottom
                             // (2*PI) end -- Current's value right-aligns
                             // here (RB-DRV-08); band drawn at `radius` with
                             // lineWidth 12, so its outer edge is radius+6
                             // out from centerX
    double bandTargetX;     // left edge of the coloured band arc's bottom
                             // end, mirrored -- Target's value left-aligns
                             // here (RB-DRV-08)
    double bandTopY;        // topmost point of the coloured band arc, above
                             // the "0" reading -- Current/Target's value top
                             // edges align here (RB-DRV-08); the actual
                             // baseline is computed by the caller from this
                             // Y plus the drawn text's own ascent (font-
                             // metric ascent isn't pure geometry, so it
                             // can't live in this struct -- see ui_driver.cpp)
    double totalBaseline;   // Total average speed / Total distance
    double tripBaseline;    // Trip average speed / Trip distance
    double captionBaseline; // column captions, under the value rows
    double boxY;            // ahead/behind readout box, top edge
    double boxHeight;       // ahead/behind readout box height
    double footBaseline;    // fps/cpu footer baseline
    double footSize;        // fps/cpu footer font size
};

CompactGaugeLayout computeCompactGaugeLayout(double width, double height);

// Caption for the driver gauge's distance column. The caption carries the
// unit rather than each value repeating it, so it has to follow the
// automatic m/km switch that formatDistanceAutoUnit() applies to the values.
std::string distanceColumnCaption(const char* unit);

// Caption for the driver gauge's average-speed column, following the
// KPH/MPH setting from the date/time screen.
std::string speedColumnCaption(bool units_mph);

// Geometry of the gauge needle for a given ahead/behind reading. Pure
// arithmetic so the clamping and angle mapping can be unit-tested without a
// display. The needle is a constant-width bar the same width as the major
// ticks, so it reads as "which tick am I on" rather than "roughly this
// direction".
struct NeedleGeometry {
    double angle;      // radians, Cairo convention (3*PI/2 is straight up)
    double length;     // hub to tip
    double halfWidth;  // half the bar's width
};

NeedleGeometry computeNeedleGeometry(double seconds, double max_seconds, double radius);

// The gauge's effective sweep, in seconds: fixed at 3 while the reading is
// within it (the needle deflects normally, exactly like pristine's green
// scale), then tracks |seconds| exactly -- not a preset -- up to a cap of
// 30. Feeding this into computeNeedleGeometry() as max_seconds is what pins
// the needle horizontal for any reading past 3s: seconds/max_seconds is
// then always exactly +-1, with no separate clamp needed anywhere else. The
// tick count (one per second of this value) grows around the pinned needle
// instead of the needle continuing to sweep a fixed dial.
double gaugeEffectiveMaxSeconds(double seconds);

// Which zone the reading is in: 0 = green (|seconds| < 10, still deflecting
// or just pinned), 1 = amber (10 to 30), 2 = red (30 and beyond, capped).
// Drives the arc colour, the scale-end chevron count, and the digital box's
// mm:ss/decimal format -- pristine read a single persisted, debounced
// data->gaugeScale (0/1/2) for all three; this supplies the same numbering,
// computed fresh from the reading every frame instead.
int gaugeZone(double seconds);

// How far a reading must clear a zone boundary before the gauge leaves the
// zone it is already showing. Entering is instant; only leaving is damped.
constexpr double GAUGE_ZONE_DEAD_BAND_S = 0.5;

// gaugeZone() with hysteresis, given the zone currently being displayed.
// gaugeZone() alone is recomputed every frame from the raw reading and drives
// the digital format, arc colour, chevron count and tick labels, so a reading
// sitting on 10.0 or 30.0 flickers all four on every redraw. An out-of-range
// previous_zone falls back to the plain zone.
int gaugeZoneHysteretic(double seconds, int previous_zone);

// Arc colour for a zone (see gaugeZone), matching pristine's exact
// per-scale RGB triples.
struct GaugeArcColor { double r, g, b; };
GaugeArcColor gaugeArcColor(int zone);

// Numeral for a tick on the gauge, one tick per second of the current
// effective sweep. Empty for the zero tick, which the centre triangle marks
// instead.
std::string gaugeTickLabel(int index);

// True while tick numerals should be drawn at all: only in the green zone.
// Past that, the dial has grown past the point where labelling individual
// second-ticks is useful -- the arc colour is the at-a-glance signal
// instead, and a numeral next to a pinned needle would claim a precision
// the needle position no longer carries.
bool gaugeTickLabelsVisible(double seconds);

// Angle (Cairo convention, same as NeedleGeometry::angle) for the major tick
// labelled `index` seconds, given the gauge's current effective sweep
// max_val (see gaugeEffectiveMaxSeconds). Uses the same seconds/max_val
// mapping as computeNeedleGeometry() so a tick numeral "i" always sits at
// the angle a reading of exactly i seconds would put the needle at -- even
// when max_val itself is fractional (e.g. max_val = 3.9 while still
// deflecting within the inner zone). Do not divide by a truncated tick
// count instead of max_val here: that would place tick "i" at
// i/tick_count of the sweep, i.e. i * (max_val / tick_count) seconds, not
// i seconds, whenever max_val has a fractional remainder.
double gaugeTickAngle(int index, double max_val);

// Splits a ';'-separated list of numbers (as typed into the Stage Setup
// screen's speed/distance entries) into parsed doubles, skipping empty
// tokens (e.g. from a leading/trailing/doubled ';'). Order preserved.
std::vector<double> parseSemicolonList(const std::string& input);

// Pairs a parsed speed list with a parsed distance list for on_add_segment()
// (RB-SEG-03). If speeds has exactly one entry, it is broadcast to every
// distance (the original, still-supported single-speed/multi-distance use
// case). If speeds and distances have the same non-zero size, they are
// paired positionally (speed i with distance i) -- this is the fix: the
// previous code silently reused only the first parsed speed for every
// segment when multiple were entered. Any other combination (speeds empty,
// or counts differ and neither is 1) is rejected: `out` is cleared and
// false is returned, so the caller adds nothing rather than guessing.
bool buildSegmentSpeedDistancePairs(const std::vector<double>& speeds,
                                     const std::vector<double>& distances,
                                     std::vector<std::pair<double, double>>& out);

#endif // CALCULATIONS_H
