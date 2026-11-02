#ifndef SECTOR_H
#define SECTOR_H

// Italian rally type (RB-NAV-44): one stage is a sector, CO to CO, made of rows
// of type navi / pc / ct / pm / co. Everything here is new code beside the
// existing calculations: nothing in calculations.cpp is edited. Two clocks: the sector
// clock runs from the departure minute and nothing moves it; the pc / pm clock runs from
// the press of its start tube. A `next` pins sector total to the line's card km, as `set` does.

#include <cstdint>
#include <string>
#include <vector>
#include "rally_state.h"

enum SegmentType { SEG_LEG = 0, SEG_NAVI, SEG_PC, SEG_CT, SEG_PM, SEG_CO };

// "navi" / "pc" / "ct" / "pm" / "co", "" for a greek leg.
std::string typeLabel(int type);
// The tag with its ref: "pc5", "pm1", "ct6", "co2", "navi", "" for a leg.
std::string sectorTag(const Segment& s);
// navi -> pc -> ct -> pm -> co -> navi (a tap on the type tag steps it).
int stepSegmentType(int type);
// A new row's ref: the last ref of the same type + 1 (1 when there is none).
int nextRef(const std::vector<Segment>& segs, int type);

// True when any row is not a leg: the stage is a sector.
bool stageIsSector(const std::vector<Segment>& segs);
// True for a pc, and for the first row of a pm (a pm row after a pm of the same ref continues it).
bool rowStartsTube(const std::vector<Segment>& segs, long i);

// Seconds a row takes at its own speed.
double rowSeconds(const Segment& s);

// RB-SEG-16 for an italian stage: adds each distance as insertStageDistances does. A
// distance inside a pc makes two pcs (card time shared by distance, later pcs renumbered);
// inside a pm, two rows of the same pm. A new row
// takes the type of the row above (navi at the top) and, for a timed or controlled
// row, the next ref of that type. Returns the notice ("" when every distance was placed).
std::string insertSectorDistances(std::vector<Segment>& segs, double first_from_m,
                                  const std::vector<double>& to_m);

// A typed duration as whole seconds: "31", "0:31", "2:12", "1:02:03"; -1 for anything else.
long parseDurationSeconds(const std::string& text);

// Applies the italian row rules: every row changes speed and none advances by
// itself (each waits for `next`); a pc's speed comes from its typed duration;
// a pm keeps its typed speed; navi / ct / co rows get the speed that fits the
// sector's duration (`sector_duration_s`, -1 = not set: the typed speed stays).
void applySectorRules(std::vector<Segment>& segs, long sector_duration_s);

// The minute a stage start press counts as (11:30:40 -> 11:30:00), as the time card records it.
int64_t italianStageStartMs(int64_t press_ms);

// Where the sector clock reads zero: the middle of the departure minute. The crew is on
// time anywhere inside the co's arrival minute, so zero here leaves 30 s either way.
const int64_t SECTOR_AIM_MS = 30000;
int64_t sectorClockZeroMs(const RallyState& s);
// The co's in-time as the card has it: the departure minute plus the card's sector duration.
int64_t coInTimeMs(const RallyState& s);

// Starts the sector: the departure minute is the minute pressed (as the card records it) and
// the start time is 30 s through it (sectorClockZeroMs); notes when the sector
// began and the card's duration (for the co's in-time), and starts the pc / pm clock at
// the real press when the sector opens on one. Only when the rally type is italian and the stage is a sector.
void markSectorStart(RallyState& s);

// "next" on an italian sector, at any line. Records the pc just finished (error against the
// card time, from its tube, and the card distance), pins sector total to the line's card km,
// and starts the pc / pm clock at a tube (a pm continuing the same ref keeps it). Never moves
// a clock and never changes a row's length. Returns true when it handled the press (the
// caller then skips today's next); false, nothing changed, on a greek stage.
bool applySectorNext(RallyState& s, uint64_t c1, uint64_t c2, int64_t now_ms);
// "prev" straight after an italian next: puts every field back. False when there is nothing to undo.
bool undoSectorTube(RallyState& s);

// Seconds left to the end of the row being driven: inside a pc / pm from its start tube, otherwise
// from the sector clock's zero (on the co row: the middle of the in-minute minus now). Invalid when
// no row is being driven.
double sectorSecondsToLine(const RallyState& s, int64_t now_ms, bool* valid);

// pc countdown beeps (owner 2026-10-30, built 2026-10-31): inside a pc only, by the
// pc's own clock from its start tube. Seconds to its end line; *valid false elsewhere.
double pcCountdownSeconds(const RallyState& s, int64_t now_ms, bool* valid);

// One beep at 30 s and 15 s to the line, then 5, 4, 3, 2, 1, and a short beep on 0.
// `prev_s` and `now_s` are successive pcCountdownSeconds readings; a beep is due when
// a mark is crossed between them. Marks already gone when the pc starts are never heard.
enum class PcBeep { None, Tick, Zero };
PcBeep pcCountdownBeep(double prev_s, double now_s);

// Navi total: the running count a stage start does not zero.
int64_t odometerCounts(const RallyState& s, uint64_t c1, uint64_t c2);
// True when prev can undo an italian next.
bool sectorUndoAvailable(const RallyState& s);

// Makes navi total read `metres` now, by solving for the correction as `set` does for Total.
// False (nothing changed) for a negative distance.
bool setOdometerMetres(RallyState& s, uint64_t c1, uint64_t c2, double metres);

// Notes navi total as the sector starts: the roadbook position is navi minus this.
void markSectorOdometer(RallyState& s, uint64_t c1, uint64_t c2);

// Zero navi total here, keeping the car's roadbook position (the readout below) where it was.
void resetOdometerKeepingPosition(RallyState& s, uint64_t c1, uint64_t c2);

// The car's roadbook position: sector total (Total with its corrections), metres.
double sectorPositionM(const RallyState& s, uint64_t c1, uint64_t c2);
// Seconds ahead (+) or behind (-) the sector schedule: the schedule's elapsed time at the car's
// position minus the real time since the sector clock's zero (sectorClockZeroMs), which nothing
// after the start moves. Invalid when no sector is being driven or the roadbook has a hole.
double sectorAheadSeconds(const RallyState& s, uint64_t c1, uint64_t c2, int64_t now_ms, bool* valid);
// The same inside a pc / pm, from its start tube (trial_start_ms / trial_start_m); invalid outside one.
double trialAheadSeconds(const RallyState& s, uint64_t c1, uint64_t c2, int64_t now_ms, bool* valid);
// The trial figure inside a pc / pm, the sector figure everywhere else (the driver needle).
double italianAheadSeconds(const RallyState& s, uint64_t c1, uint64_t c2, int64_t now_ms, bool* valid);

// Zero navi total here (the Total key at rest, a power loss).
void zeroOdometer(RallyState& s, uint64_t c1, uint64_t c2);

// The driver needle in italian: the pc / pm error inside one; invalid (needle at rest, tone silent)
// on navi / ct / co subs, where crews drive flat out and a lead of many minutes is normal.
double italianNeedleSeconds(const RallyState& s, uint64_t c1, uint64_t c2, int64_t now_ms, bool* valid);
// The figure the driver gauge, its tone and the phone all show: on an italian sector
// italianNeedleSeconds (0 at rest outside a pc / pm; A-B zero is off in italian); otherwise
// calculateAheadBehindFromStageStart on `corrected_stage_counts` plus the A-B / B-C zero offset.
double gaugeAheadSeconds(const RallyState& s, uint64_t c1, uint64_t c2, int64_t now_ms,
                         int64_t corrected_stage_counts);
// Whole minutes in hand (or behind) for the "+/-" box: |ahead| / 60 rounded, at most 99.
long minutesInHand(double ahead_s);
// The three readout keys on Home: row 0 total, 1 trip, 2 countdown; italian (rally_type 2)
// reads "sector\ntotal", "sub", "navi".
const char* readoutKeyName(int rally_type, int row);   // rows 3, 4: the next and prev keys
// True when the italian page should draw the idle preview: no sector running and a sector loaded.
bool sectorPreviewWanted(const RallyState& s);

// Everything the italian navi page shows beside its keys.
struct SectorPanel {
    enum Kind { NAVI, PC, PM } kind = NAVI;
    bool running = false;          // a stage is under way
    std::string inTag;             // the row being driven: "pc5"
    double inKph = 0.0;
    std::string nextTag;           // the row after: "pc6", "navi", "co2"
    long nextAtM = 0;              // sector distance where it starts
    long metresToGo = 0;           // to the end of this row
    double secondsToLine = 0.0;    // to the end of the row, by the schedule (pc: the card time)
    bool secondsValid = false;
    double avgKph = 0.0;           // pc: average so far over this pc
    bool coValid = false;          // the co's arrival
    int coRef = 0;
    int64_t coInTimeMs = 0;        // sector start + sector duration (rally ms)
    int coKph = 0;                 // speed that gets there on its in-time
    long coMinutes = 0;            // minutes left to it
};
SectorPanel sectorPanel(const RallyState& s, int64_t now_ms, long sector_m);

#endif
