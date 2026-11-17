#ifndef STAGE_STORE_H
#define STAGE_STORE_H

#include <string>
#include <vector>
#include "rally_state.h"

// Stages are numbered by the organiser, 1-99 (RB-NAV-45). A stage is
// "programmed" when its memory holds a roadbook.
bool validStageNumber(int n);

// Programmed stage numbers, ascending.
std::vector<int> programmedStages(const RallyState& s);

// What was typed in the Choose Stage popup as a stage number (1-99), or 0
// when it is empty, not a number, or out of range.
int chosenStageNumber(const std::string& text);

// True when going from one distance unit to the other rescales programmed stages (km or metres <-> miles).
bool unitChangeRescalesStages(int old_unit, int new_unit);

// The Settings warning before a km <-> miles change, worded for the direction chosen.
std::string unitChangeConfirmText(bool to_miles);

// Switch the distance unit (owner, 2026-11-15). km or metres <-> miles reinterprets every programmed stage -- the
// figures stay, the unit changes (a 5 km / 5000 m stage becomes 5 miles); Total, Trip and the calibration keep their
// real values. Refused (false, nothing changed) when that would rescale a stage while one is live. A change of format
// inside km/metres or inside miles just changes the unit.
bool changeDistanceUnit(RallyState& s, int new_unit);

#endif
