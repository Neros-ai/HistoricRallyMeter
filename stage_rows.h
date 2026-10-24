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
    long number = 0;        // roadbook number: first_number + index
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

// "1,000;1,500;2,300" -> metres. ';' separates, ',' is a thousands separator
// and is ignored, spaces ignored. Tokens must be positive and each greater
// than the one before (the first greater than `after_m`). Parsing stops at the
// first bad token; `accepted` holds the good ones before it and `rest` the
// text from the bad token on (empty when everything was taken).
struct DistanceParse {
    std::vector<double> accepted;
    std::string rest;
};
DistanceParse parseStageDistances(const std::string& text, double after_m);

// Append one row per distance (each a "to m" measured from the stage's start
// frame, i.e. absolute roadbook metres). A new row carries the speed above and
// is not a speed change. `segs` must already hold the rows the distances follow.
void appendStageDistances(std::vector<Segment>& segs, double first_from_m,
                          const std::vector<double>& to_m);

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

// Metres of travel from the stage start at which each row ends, for Beep
// Assist: every row beeps.
std::vector<double> deriveWaypoints(const std::vector<Segment>& segs);

// Fills target_speed_counts_per_hour and distance_counts from kph / metres.
void recomputeSegmentCounts(std::vector<Segment>& segs, long calibration);

// "2:00", "0:58", "62:05" for a number of seconds (rounded).
std::string formatMmSs(double seconds);

// "10:45" or "10:45:30" -> seconds after midnight; -1 for anything else.
long parseTimeOfDay(const std::string& text);
// "6,200", "6200 m" -> whole metres; -1 for anything else.
long parseMetres(const std::string& text);

#endif // STAGE_ROWS_H
