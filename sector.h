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
                                  const std::vector<double>& to_m, bool km = false);

// A typed duration as whole seconds: "31", "0:31", "2:12", "1:02:03"; -1 for anything else.
long parseDurationSeconds(const std::string& text);

// Applies the italian row rules: every row changes speed and none advances by
// itself (each waits for `next`); a pc's speed comes from its typed duration;
// a pm keeps its typed speed; navi / ct / co rows get the speed that fits the
// sector's duration (`sector_duration_s`, -1 = not set: the typed speed stays).
void applySectorRules(std::vector<Segment>& segs, long sector_duration_s);

// The second a stage start press counts as: the press itself (11:30:40 stays 11:30:40).
int64_t italianStageStartMs(int64_t press_ms);

// The sector clock reads zero at the second of the departure press. The box aims for the
// same second of the co's arrival minute (press + card); the arrival minute is managed by the crew.
int64_t sectorClockZeroMs(const RallyState& s);
// The co's in-time: the departure second plus the card's sector duration.
int64_t coInTimeMs(const RallyState& s);

// Starts the sector: the departure minute is the minute pressed (as the card records it) and
// the start time is its :00 (sectorClockZeroMs); notes when the sector
// began and the card's duration (for the co's in-time), and starts the pc / pm clock at
// the real press when the sector opens on one. Only when the rally type is italian and the stage is a sector.
void markSectorStart(RallyState& s);

// "next" on an italian sector, at any line. Records the pc just finished (error against the
// card time, from its tube, and the card distance), pins sector total to the line's card km,
// and starts the pc / pm clock at a tube (a pm continuing the same ref keeps it). Never moves
// a clock and never changes a row's length. Returns true when it handled the press (the
// caller then skips today's next); false, nothing changed, on a greek stage.
bool applySectorNext(RallyState& s, uint64_t c1, uint64_t c2, int64_t now_ms);
// RB-NAV-52: an italian sector ends only when the crew press next on its last row (the co), never
// on distance: an over-reading box or a wrong turn must not take the co panel away early.
// True for every stage except a running italian sector (those keep ending on distance as today).
bool sectorEndsOnDistance(const RallyState& s);
// next is live on the last row of a running italian sector (it is the finishing press).
bool sectorFinishAvailable(const RallyState& s);
// The finishing press: ends the sector (stage_complete) and drops the tube undo, so prev stays
// greyed after the finish. False, nothing changed, when not available.
bool applySectorFinish(RallyState& s);
// "prev" straight after an italian next: puts every field back. False when there is nothing to undo.
bool undoSectorTube(RallyState& s);

// Seconds left to the end of the row being driven: inside a pc / pm from its start tube, otherwise
// from the sector clock's zero (on the co row: the arrival second minus now). Invalid when
// no row is being driven.
double sectorSecondsToLine(const RallyState& s, int64_t now_ms, bool* valid);

// Press-warning countdown: inside a pc, by the pc's own clock from its start tube;
// on a navi row, by the sector clock, so the crew hears that next is due.
// Seconds to the end line; *valid false elsewhere (ct, pm, co, greek).
double pcCountdownSeconds(const RallyState& s, int64_t now_ms, bool* valid);

// The navigation double beep at 5 s to the line, a single beep at 4, 3, 2 and 1,
// and a shorter beep on 0 (no 30 s or 15 s warning).
// `prev_s` and `now_s` are successive pcCountdownSeconds readings; a beep is due when
// a mark is crossed between them. Marks already gone when the row starts are never heard.
enum class PcBeep { None, Tick, Five, Zero };
PcBeep pcCountdownBeep(double prev_s, double now_s);
// The sound card and the tone thread delay a beep by roughly this long, so the countdown marks are
// fired this much early and are heard on the second. Tune on the Pi by recording the beep.
const double PC_BEEP_LEAD_S = 0.10;

// Metres left to the end of the row being driven. Valid on a running italian row that
// is not a pc (navi, ct, pm, co). Negative once the line is passed.
double rowMetresToLine(const RallyState& s, double stage_m, bool* valid);
// One beep as the car crosses 100 m still to go. A row that starts inside 100 m stays silent.
bool hundredMetreBeep(double prev_m, double now_m);

// Navi total: the running count a stage start does not zero.
int64_t odometerMm(const RallyState& s, uint64_t c1, uint64_t c2);   // navi, in millimetres
// True when prev can undo an italian next.
bool sectorUndoAvailable(const RallyState& s);

// Makes navi total read `metres` now, by solving for the correction as `set` does for Total.
// False (nothing changed) for a negative distance.
bool setOdometerMetres(RallyState& s, uint64_t c1, uint64_t c2, double metres);

// The +10 / -10 keys on navi: moves navi total by delta_mm, in every rally type. A -10 cannot take
// navi below zero (it stops at 0), and does nothing to a navi that reverse has already put below
// zero; +10 always adds.
void adjustOdometerMm(RallyState& s, long delta_mm, uint64_t c1, uint64_t c2);

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
// The same inside a pc / pm, from its start tube (trial_start_ms / trial_start_m).
// Invalid outside one. A point still short of the tube is behind, including a car sat at the press.
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
// calculateAheadBehindFromStageStart on `stage_mm` (stagePositionMm) plus the A-B / B-C zero offset.
double gaugeAheadSeconds(const RallyState& s, uint64_t c1, uint64_t c2, int64_t now_ms,
                         int64_t stage_mm);
// Whole minutes in hand (or behind) for the "+/-" box: |ahead| / 60 rounded, at most 99.
long minutesInHand(double ahead_s);
// The three readout keys on Home: row 0 total, 1 trip, 2 countdown; italian (rally_type 2)
// reads "sector\ntotal", "sub", "navi".
const char* readoutKeyName(int rally_type, int row);   // rows 3, 4: the next and prev keys
// True when the italian page should draw the idle preview: no sector running and a sector loaded.
bool sectorPreviewWanted(const RallyState& s);
// The idle card as it will look when the sector starts: a copy of `live` on the sector's first
// row, with the start times at now_ms and the sector marked started. The live state is never touched.
RallyState sectorPreviewState(const RallyState& live, int64_t now_ms);

// Everything the italian navi page shows beside its keys.
struct SectorPanel {
    enum Kind { NAVI, PC, PM } kind = NAVI;
    bool running = false;          // a stage is under way
    std::string inTag;             // the row being driven: "pc5"
    double inKph = 0.0;
    std::string nextTag;           // the row after: "pc6", "navi", "co2"
    long nextAtM = 0;              // sector distance where it starts
    long metresToGo = 0;           // to the end of this row
    long metresPast = 0;           // beyond the end of this row (display only, 0 before it)
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

// The sub (trip) average speed in km/h, from the sub's own start: what a navi row shows in its
// countdown line, since a navi has no tube to average from.
double subAverageKph(const RallyState& s, uint64_t c1, uint64_t c2, int64_t now_ms);

// Italian, the row being driven is a pc or a pm: resetting sub now would spoil the measurement,
// so the reset key asks first.
bool subResetInStage(const RallyState& s);
// Italian, a sector running: the Total key does not reset (owner 2026-11-12). Every `next` pins
// sector total to the card and a start zeroes it at the line, so a zero mid-sector could only
// throw the sector back to its first row; `set`, `next` and `prev` are the corrections.
bool sectorTotalResetBlocked(const RallyState& s);
extern const char* const SECTOR_TOTAL_RESET_NOTICE;
// RB-WEB-21: the count down figures as the box prints them, so the phone can show the same text.
// km to the line with two decimals; "+0.04" once the line has gone by.
// In metres (the Settings distance unit) it is whole metres, "+40" once past.
std::string sectorCountdownKmText(const SectorPanel& p, bool km);
// The italian card's figures in the units chosen in Settings. km: cut down to the 10 m, "1.24"
// (never rounded up); metres: whole metres. Speeds: two decimals in kph, or mph when mph is set.
std::string sectorDistText(double metres, bool km);
std::string sectorSpeedText(double kph, bool mph);
int sectorSpeedWhole(int kph, bool mph);
// m:ss.t; "+m:ss.t" when negative (the line has gone by).
std::string sectorSecondsText(double s);
std::string sectorSecondsWholeText(double s);   // the same, no tenths

// The italian sector card as the phone draws it. "null" when the box is not italian.
// Idle with a sector loaded is the same preview the box draws; nothing is written.
std::string sectorCardJson(const RallyState& live, int64_t now_ms, long stage_m,
                           uint64_t c1, uint64_t c2);

#endif
