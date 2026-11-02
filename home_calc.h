#ifndef HOME_CALC_H
#define HOME_CALC_H

// Pure helpers behind the greek Home page (RB-NAV-46). New code beside the
// calculations; nothing in calculations.cpp is touched.

#include <string>
#include <vector>
#include "rally_state.h"

// Home's Trip history shows the newest four readings.
constexpr size_t HOME_HISTORY_ENTRIES = 4;
std::vector<long> homeTripHistory(const RallyState& s);

// Seconds, driving at the segments' own speeds, until the next speed change:
// `remaining_m` left in segment `cur`, then every following row that carries
// the speed on (changesSpeed false), up to the row where the speed changes.
// `*valid` is false when there is no later change, the speed is 0 or `cur` is out of range.
double nextChangeSeconds(const std::vector<Segment>& segs, long cur, double remaining_m, bool* valid);

// Index of the next row where the speed changes after segment `cur`, skipping
// rows that carry the speed on; -1 when none follows or `cur` is out of range.
long nextChangeIndex(const std::vector<Segment>& segs, long cur);

// Metres to the next speed change: `remaining_m` left in segment `cur` plus
// every following row that carries the speed on. With no later change it runs
// to the end of the stage. The countdown figure counts to speed changes, not
// to the end of each row.
long metresToNextChange(const std::vector<Segment>& segs, long cur, long remaining_m);

// "next speed" (greek / surrey): fold the rows that carry the speed on after
// segment `cur` into it, so the following row is the next speed change. Their
// lengths add to `cur`, so every later change point stays where it was.
// Returns the number of rows folded (0 when the next row already changes speed
// or no later change follows).
long foldCarriedRows(std::vector<Segment>& segs, long cur);

// "02:11": two figures each, minutes may pass 99.
std::string formatClockMmSs(double seconds);

// A target / next speed as the driver display shows it: one decimal, two in the italian
// rally type (the time card prints 24.26). Display only.
std::string driverSpeedText(double speed, int rally_type);

// The A-B, B-C zero key is on the greek page when Measure type is AB,BC; the
// single-display layout has no room for it.
bool showAdjZeroKey(const RallyState& s, bool single_display);

#endif
