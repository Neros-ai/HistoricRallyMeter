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

// Distance from the stage start to the end of row `idx`, in whole millimetres (the rows' typed metres summed).
int64_t stageRowEndMm(const std::vector<Segment>& segs, long idx);

// The countdown shown (greek and surrey): the end of the row the speed next changes at, measured from the
// stage start and cut to whole metres, minus Total as shown. Total + countdown therefore always equals the
// roadbook figure. `cur` out of range gives 0.
long countdownMetres(const std::vector<Segment>& segs, long cur, long total_shown_m);

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

// What the Home stage panel calls the start: "auto" while an autostart is armed
// for this stage, or for the running stage if an autostart started it; "manual"
// otherwise (including a stage started by start now).
enum class StartWord { AUTO, MANUAL };
StartWord homeStartWord(bool armedPending, bool firedThisStage, bool startedByAuto);

// Font size (26 down to 18) for the start time so that, beside the word, it
// keeps a 12 px gap inside the column: 4 + wordWidth + 12 + width <= columnW - 4.
// timeWidthAt26 is the time's width at 26 px; width scales with the font.
double homeStartTimeFont(double wordWidth, double timeWidthAt26, double columnW);

#endif
