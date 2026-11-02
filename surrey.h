#ifndef SURREY_H
#define SURREY_H

// Surrey rally type (RB-NAV-43): every surrey rule lives here, beside the existing
// calculations; nothing in calculations.cpp is edited.

#include <cstdint>
#include <string>
#include "rally_state.h"

// rally_type 1.
bool isSurrey(const RallyState& s);

// Navi total (the italian odometer, which a stage start never zeroes) with its Set Navi
// correction, whole metres. Total and its corrections are separate and untouched by it.
long surreyNaviMetres(const RallyState& s, uint64_t c1, uint64_t c2);

// Trip history readings shown on Home: four, in every rally type (surrey's foot line is time taken).
int tripHistoryRows(const RallyState& s);

// A stage's time taken as the organiser's form has it: "9:20.6", "1:02:03.4" from an hour,
// to the nearest 0.1 s. Negative input reads as zero.
std::string surreyTimeTakenText(int64_t ms);

// The countdown panel's "min" figure: metres to go at the speed in force, "mm:ss"; "" at no speed.
std::string surreyCountdownTime(double metres, double kph);

#endif
