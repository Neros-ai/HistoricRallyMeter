#ifndef SURREY_H
#define SURREY_H

// Surrey rally type (RB-NAV-43): every surrey rule lives here, beside the existing
// calculations; nothing in calculations.cpp is edited.

#include <cstdint>
#include <string>
#include <vector>
#include "rally_state.h"

// rally_type 1.
bool isSurrey(const RallyState& s);

// Surrey's Total as the screens show it (owner 2026-11-12): the roadbook's running distance, which
// a stage start never zeroes -- the count italian calls navi -- with its `set` correction, whole
// metres. The third figure, "stage", is the distance since the `now` press (the count greek calls
// Total), which the stage itself runs on exactly as in greek.
long surreyNaviMetres(const RallyState& s, uint64_t c1, uint64_t c2);
// Stage arrival: metres still to go to the loaded stage's start (its "Distance at start"), for Home,
// the driver display and the phone alike. Measured on the Total each rally type shows: surrey's
// roadbook Total, greek's Total. Zero or less once the start is passed.
long metresToStageStart(const RallyState& s, uint64_t c1, uint64_t c2, long start_distance_m);

// True while a surrey stage is live: running, or an early departure counting down.
bool surreyStageLive(const RallyState& s);
// `set` in surrey: pins Total (the roadbook figure) to `metres`. While a stage is live the stage
// distance moves by the same amount, so the two always agree about where the car is. False (nothing
// changed) for a negative figure.
bool surreySetTotal(RallyState& s, uint64_t c1, uint64_t c2, double metres);
// The Total key does not reset while a surrey stage is live: Total is the roadbook figure, corrected
// with `set`. Outside a stage the key zeroes it (where the roadbook restarts).
bool surreyTotalResetBlocked(const RallyState& s);
extern const char* const SURREY_TOTAL_RESET_NOTICE;

// Trip history readings shown on Home: four, in every rally type (surrey's foot line is time taken).
int tripHistoryRows(const RallyState& s);

// A stage's time taken as the organiser's form has it: "9:20.6", "1:02:03.4" from an hour,
// to the nearest 0.1 s. Negative input (no result) gives "".
std::string surreyTimeTakenText(int64_t ms);

// The countdown panel's "min" figure: metres to go at the speed in force, "mm:ss"; "" at no speed.
std::string surreyCountdownTime(double metres, double kph);

// Start landmark (D1): the press may lie this far before the start box figure and still fit row 1.
const double SURREY_START_SLACK_M = 300.0;

// Shorten the running copy's row 1 so it still ends at its roadbook "to" when the stage starts
// navi_at_press_m (navi total) instead of first_from_m. True when navi lies from
// first_from_m - SURREY_START_SLACK_M up to (not including) row 1's "to"; otherwise unchanged, false.
bool fitStageToStart(std::vector<Segment>& stage_segs, double first_from_m, double navi_at_press_m, long calibration);

// A surrey stage starts (a `now` press, or the arming of an early departure): fit row 1 of the running
// copy to navi total at sector_start_odometer_mm and flag a failed fit. The last time taken is kept
// until the next stage finishes.
void surreyStart(RallyState& s, const RallyState::MemorySlot& slot);

// `next` on the last row of a running surrey stage finishes it and freezes time taken (D3).
// Anything else: false, nothing changed, the caller runs today's `next`.
bool applySurreyFinish(RallyState& s, int64_t now_ms);

// `prev` reopens a pressed finish while navi total (navi_m) has not passed the last row's "to".
bool undoSurreyFinish(RallyState& s, double navi_m, double last_to_m);

// Where greek ends a stage on distance (D4): in surrey, take time taken then unless a press did.
void surreyDistanceEnd(RallyState& s, int64_t now_ms);

// Key sensitivity (pure): `next` is live as the finish on the last row of a running surrey stage.
bool surreyFinishAvailable(const RallyState& s);
// Same conditions as undoSurreyFinish, no mutation: `prev` is live to reopen a pressed finish.
bool surreyUndoAvailable(const RallyState& s, double navi_m, double last_to_m);
// As above, finding navi total from the counters and the last "to" from the loaded stage's slot.
bool surreyUndoAvailableNow(const RallyState& s, uint64_t cntr1, uint64_t cntr2);

// The time-taken text for the stage log when a stage stops: only a stage that really ended (last row,
// complete, figure belongs to the loaded slot). An abort leaves segment -1 and gives "".
std::string surreyLogTimeTaken(const RallyState& s);

// The last row's "to" in roadbook metres: the slot's from figure plus its typed row distances.
double surreyLastToMetres(const RallyState::MemorySlot& slot);

// Distance the Beep Assist waypoints are measured against. Waypoints are stage distances from the
// start box, so surrey takes navi total (roadbook km, which a stage start never zeroes) less the
// loaded stage's start box figure (first_from_m); greek and italian use stage_travelled_m as today.
double beepDistanceM(const RallyState& s, uint64_t c1, uint64_t c2, double stage_travelled_m);

#endif
