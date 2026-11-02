#ifndef REACTION_OFFSET_H
#define REACTION_OFFSET_H

// Reaction time offset (RB-SET-12): a Settings figure, -300..+300 ms, that corrects a crew's
// consistent press lag. A press that marks a point the car is passing is taken as happening
// `offset` earlier in time and `speed x offset` back in distance. Everything here is applied by
// the callers to the values they hand on; nothing in calculations.cpp knows about it.

#include <cstdint>
#include "rally_state.h"

const int REACTION_OFFSET_MAX_MS = 300;

// Clamp a stored or typed figure to -300..+300.
int clampReactionOffsetMs(long ms);

// The time the press is taken to have happened.
int64_t pressTimeMs(int64_t press_ms, int offset_ms);

// How far back the car was, in metres / centimetres / counts (never negative for a late press).
double pressBackMetres(double speed_mps, int offset_ms);
long pressBackCm(double speed_mps, int offset_ms);
int64_t pressBackCounts(double speed_mps, int offset_ms, long calibration);

// The display speed (kph, or mph when `mph`; negative = no reading) in metres per second.
double reactionSpeedMps(double displayed_speed, bool mph);

// Italian `next` into a tube: applySectorNext set trial_start_ms to the press; take it back by the
// offset. `trial_before` is trial_start_ms as it stood before that call (nothing started: unchanged).
void shiftTubeStart(RallyState& s, int64_t trial_before, int64_t press_ms);

// A start press at a point (start now as a flying start): greek and surrey take the stage clock and
// distance baselines back; italian only takes its pc / pm clock (trial_start_ms) back when a tube starts.
void applyStartPressOffset(RallyState& s, double speed_mps);

// Arming an early departure presses at the start point too: distance only (the clock is the minute).
void applyArmPressOffset(RallyState& s, double speed_mps);

// Greek `next`: the driven counts handed to the boundary move, taken back by speed x offset.
int64_t boundaryCountsAtPress(int64_t driven_counts, double speed_mps, int offset_ms, long calibration);

#endif
