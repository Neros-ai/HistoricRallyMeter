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

#endif
