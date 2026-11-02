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

// A start now counts as a flying start only when the car is moving: at or above this speed.
const double REACTION_FLYING_MIN_KPH = 5.0;

// A start press at a point (start now as a flying start, only when moving): greek and surrey take
// the stage clock and distance baselines back; italian (a sector stage) takes only its pc / pm
// clock (trial_start_ms) back when a tube starts. A standing start is untouched.
void applyStartPressOffset(RallyState& s, double speed_mps);

// Greek / surrey `next`, after the new segment's baselines were set at the press: the segment began
// speed x offset ago, so its distance baseline (and the trip's) sit that far back, matching the
// time baseline already taken back. Without it boundary k drifts by (k-1) x speed x offset.
void applyNextBaselineOffset(RallyState& s, double speed_mps);

// Greek `next`: the driven counts handed to the boundary move, taken back by speed x offset.
int64_t boundaryCountsAtPress(int64_t driven_counts, double speed_mps, int offset_ms, long calibration);

#endif
