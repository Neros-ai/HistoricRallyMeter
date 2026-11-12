#ifndef STAGE_ROWS_H
#define STAGE_ROWS_H

// The Nero2.0 Segments page as plain data: a stage is a list of rows, each one
// leg of the roadbook. The rows are views of std::vector<Segment> (one Segment
// per row); every edit goes through the functions here so the table, the
// timing engine and the beep distances can never disagree.
//
// Nothing in this file touches GTK.

#include <string>
#include <vector>
#include "rally_types.h"

struct StageRow {
    long number = 0;        // roadbook box: typed (Segment::box), else the row above + 1; row 1 = first_number
    double from_m = 0;      // where the leg starts (first_from_m for row 1)
    double to_m = 0;        // where it ends: the distance typed into "add distance"
    double dist_m = 0;      // leg length
    bool chg = true;        // the speed changes here (row 1 always)
    double kph = 0;
    long ms = 0;            // whole metres per second, shown where chg
    bool auto_available = false;   // chg and not row 1
    bool auto_on = false;
    double time_s = 0;      // time for this leg at its speed
    double total_s = 0;     // running sum of time_s
};

std::vector<StageRow> buildStageRows(const std::vector<Segment>& segs,
                                     long first_number, double first_from_m);

// "1,000;1,500;2,300" -> metres. ';' separates, spaces ignored; in metres ','
// is a thousands separator and ignored. With `km` the figures are kilometres
// ("17,23" or "17.23"), ',' or '.' the decimal, and come out in metres.
// Tokens must be positive and may come in any order (RB-SEG-16). Parsing
// stops at the first bad token; `accepted` holds the good ones before it and
// `rest` the text from the bad token on (empty when everything was taken).
struct DistanceParse {
    std::vector<double> accepted;
    std::string rest;
};
DistanceParse parseStageDistances(const std::string& text, int unit = 0);

// Append one row per distance (each a "to m" measured from the stage's start
// frame, i.e. absolute roadbook metres). A new row carries the speed above and
// is not a speed change. `segs` must already hold the rows the distances follow.
void appendStageDistances(std::vector<Segment>& segs, double first_from_m,
                          const std::vector<double>& to_m);

// RB-SEG-16: add each distance where it falls. Past the end it appends as
// appendStageDistances does; inside a row it splits that row there: the first
// half keeps the row's chg and box but has its auto switched on (it hands over to the
// new half by itself); the new second half carries the same speed with chg off and the
// row's own auto and no box. A distance at or before the stage's
// start, or on an existing row boundary, is refused. Returns a notice naming
// the refused distances ("" when every one was placed), in km when `km` is on.
std::string insertStageDistances(std::vector<Segment>& segs, double first_from_m,
                                 const std::vector<double>& to_m, int unit = 0);

void setRowSpeed(std::vector<Segment>& segs, size_t row, double kph);
// Ticks / unticks chg. Row 0 is always a change and ignores this.
void setRowChange(std::vector<Segment>& segs, size_t row, bool chg);
// The auto box of `row` (only meaningful where chg and row >= 1): the speed
// change happens by itself when this row begins.
void setRowAuto(std::vector<Segment>& segs, size_t row, bool on);
// Move the end of `row` to `new_to_m` (whole metres from the stage's frame). The
// next row then starts at the new figure and is shortened or lengthened to
// match, so every later row's end stays where it was; on the last row the stage
// simply ends elsewhere. False, and nothing changed, if the new end is not past
// this row's start or not short of the next row's end.
bool setRowTo(std::vector<Segment>& segs, double first_from_m, size_t row, double new_to_m);
// Removes a row. Its length goes to the row above so later positions do not
// move; deleting row 0 moves first_from_m instead.
void deleteRow(std::vector<Segment>& segs, double& first_from_m, size_t row);

// Rows that do not change speed take the speed above, and the row above them
// advances by itself. Called by every edit above; exposed for loaders.
void carrySpeeds(std::vector<Segment>& segs);

// The roadbook as it is driven (greek, owner 2026-10-31): a ditto row is always
// entered by itself -- auto belongs to speed changes only -- so every row before
// a ditto row gets autoNext, whatever the stored file says. Speeds carried too.
std::vector<Segment> stageForDriving(const std::vector<Segment>& roadbook);

// Metres of travel from the stage start at which each row ends, for Beep
// Assist: every row beeps.
std::vector<double> deriveWaypoints(const std::vector<Segment>& segs);


// "2:00", "0:58", "62:05" for a number of seconds (rounded).
std::string formatMmSs(double seconds);
// Always h:mm:ss: 131 -> "0:02:11", 35999 -> "9:59:59" (duration and elapsed on Segments).
std::string formatHMmSs(double seconds);
// A distance in metres as the Distance units setting shows it: grouped metres
// ("1,320") or kilometres to two decimals ("17.43").
// Distance units (Settings): how a distance is shown and typed. The box itself counts in millimetres.
// One value holds both Settings choices, the format (0.00 / 0.000 / 0,000) and the unit (km / mi):
//   DIST_M    km unit, 0,000: whole metres, grouped
//   DIST_KM   km, 0.00: two decimals, cut down to the 10 m
//   DIST_KM3  km, 0.000: three decimals, cut down to the metre
//   DIST_MI   mi, 0.000: three decimals, cut down to the 0.001 mile
//   DIST_MI2  mi, 0.00: two decimals, cut down to the 0.01 mile
// (0,000 is not offered in miles.) A three-decimal figure drops to two decimals from 100 km or
// 100 miles, which is all the big figures hold. A bool still works: false metres, true km.
enum : int { DIST_M = 0, DIST_KM = 1, DIST_MI = 2, DIST_KM3 = 3, DIST_MI2 = 4 };
constexpr double METRES_PER_MILE = 1609.344;
inline bool distIsMiles(int unit) { return unit == DIST_MI || unit == DIST_MI2; }
inline bool distIsKm(int unit) { return unit == DIST_KM || unit == DIST_KM3; }   // a km figure with decimals
inline int distDecimals(int unit) { return unit == DIST_M ? 0 : (unit == DIST_KM || unit == DIST_MI2) ? 2 : 3; }
// Put a format (decimals 2, 3 or 0 = whole) and a unit together; whole is km only (metres).
inline int distUnitFrom(int decimals, bool miles) {
    if (miles) return decimals == 2 ? DIST_MI2 : DIST_MI;
    return decimals == 0 ? DIST_M : decimals == 2 ? DIST_KM : DIST_KM3;
}
inline bool distUnitValid(int unit) { return unit >= DIST_M && unit <= DIST_MI2; }
const char* distUnitName(int unit);                 // "m", "km", "mi"
double distUnitToMetres(int unit, double figure);   // a typed figure in that unit, as metres
std::string formatDistance(double metres, int unit);

// The Segments page's text for one row, exactly as the box shows it. The box
// and the phone both use this, so the two can never read differently.
// rally_type 2 is italian; every other type uses the greek row.
struct SegRowText {
    std::string num, ref, from, to, dist, kph, dur, tot;
};
// mph: the speed column follows the Settings speed units (speeds are stored in km/h).
SegRowText segRowText(const Segment& seg, const StageRow& row, size_t index, int rally_type, int unit, bool mph = false);

// "Average 32.08" under the italian sector duration ("" without a duration or distance).
std::string sectorAverageText(const std::vector<Segment>& segs, long sector_duration_s, bool mph = false);

// The greek "Distance at start" entry: "6,000 m" / "6.00 km" ("" when not set).
std::string startDistanceText(long start_distance_m, int unit);

// "10:45" or "10:45:30" -> seconds after midnight; -1 for anything else.
long parseTimeOfDay(const std::string& text);
// "6,200", "6200 m" -> whole metres; -1 for anything else.
long parseMetres(const std::string& text);
// A typed distance as whole metres: metres as parseMetres, or with `km` kilometres
// ("17,43", "17.5 km"); -1 for anything else.
long parseDistance(const std::string& text, int unit);
// True when an entry still shows exactly what the box wrote for the stored figure. Such a field is
// never written back: the text on screen is cut to the format in use (8.345 km reads 8.34 in 0.00),
// so writing it back would shorten the programmed figure. Changing the format or the unit therefore
// never changes stored data; only a figure the crew actually type does.
bool distanceFieldUntouched(const std::string& text, double stored_m, int unit);
// The same figure kept to the millimetre (miles do not fall on whole metres), as metres; -1 unreadable.
double parseDistanceExact(const std::string& text, int unit);

#endif // STAGE_ROWS_H
