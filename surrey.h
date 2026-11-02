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

// Navi total (the italian odometer, which a stage start never zeroes) with its Set Navi
// correction, whole metres. Total and its corrections are separate and untouched by it.
long surreyNaviMetres(const RallyState& s, uint64_t c1, uint64_t c2);

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
// copy to navi total at sector_start_odometer_cm, flag a failed fit, clear the last time taken.
void surreyStart(RallyState& s, const RallyState::MemorySlot& slot);

// `next` on the last row of a running surrey stage finishes it and freezes time taken (D3).
// Anything else: false, nothing changed, the caller runs today's `next`.
bool applySurreyFinish(RallyState& s, int64_t now_ms);

// `prev` reopens a pressed finish while navi total (navi_m) has not passed the last row's "to".
bool undoSurreyFinish(RallyState& s, double navi_m, double last_to_m);

// Where greek ends a stage on distance (D4): in surrey, take time taken then unless a press did.
void surreyDistanceEnd(RallyState& s, int64_t now_ms);

// The last row's "to" in roadbook metres: the slot's from figure plus its typed row distances.
double surreyLastToMetres(const RallyState::MemorySlot& slot);

#endif
