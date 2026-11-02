#ifndef FREEZE_H
#define FREEZE_H

// Freeze (navigation error): while it is lit the box does not count distance.
// Total, Trip, the segment, the italian figures and the phone all stand still,
// and on release they carry on from the held figure -- the travel made while
// frozen is never added back. Timers and speed keep running.

#include "rally_state.h"
#include "counter_poller.h"

// Turn freeze on or off (a press before the first counter poll is ignored), keeping what a restart needs in the state.
void applyFreeze(RallyState& s, CounterPoller& p, bool on);

// At startup, after setVirtualShift: a box that was frozen comes back frozen
// on the held figure, whatever the car did while it was off.
void restoreFreeze(const RallyState& s, CounterPoller& p);

#endif
