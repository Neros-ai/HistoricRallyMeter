#include "stage_edit.h"
#include "callbacks.h"
#include "calculations.h"
#include "config_file.h"
#include "rally_state.h"
#include "sector.h"
#include "stage_phase.h"
#include "stage_rows.h"
#include "surrey.h"

int stageEditSlot(AppData* d) {
    RallyState& s = *d->state;
    if (s.last_memory_slot >= 1 && s.last_memory_slot <= RallyState::MAX_MEMORY_SLOTS)
        return s.last_memory_slot;
    int pick = RallyState::MAX_MEMORY_SLOTS;
    for (int i = 0; i < RallyState::MAX_MEMORY_SLOTS; i++)
        if (s.memory_slots[i].empty()) { pick = i + 1; break; }
    if (!s.segments.empty() && s.memory_slots[pick - 1].empty()) {
        s.memory_slots[pick - 1].segments = s.segments;
        s.memory_slots[pick - 1].beep_waypoints_m = deriveWaypoints(s.segments);
        s.memory_slots[pick - 1].waypoints_recorded = true;
    }
    s.last_memory_slot = pick;
    return pick;
}

bool stageEditLocked(const RallyState& st) { return stageLive(st); }

#define STAGE_EDIT_GUARD(d, ret) if (stageEditLocked(*(d)->state)) return ret

static RallyState::MemorySlot& slotOf(AppData* d) {
    return d->state->memory_slots[stageEditSlot(d) - 1];
}

void stageEditCommit(AppData* d) {
    RallyState& s = *d->state;
    RallyState::MemorySlot& slot = slotOf(d);
    // Italian: the row rules (every row waits for next, speeds worked out from the card).
    if (isItalian(s) && stageIsSector(s.segments)) applySectorRules(s.segments, slot.sector_duration_s);
    s.beep_waypoints_m = deriveWaypoints(s.segments);
    slot.segments = s.segments;
    slot.beep_waypoints_m = s.beep_waypoints_m;
    slot.waypoints_recorded = true;
    d->tickMem.beepCursorsStale = true;
    adoptRoadbookIfIdle(d);
    saveAndNotify(d);
}

void stageEditSelect(AppData* d, int slot_1) {
    STAGE_EDIT_GUARD(d, );
    if (slot_1 < 1 || slot_1 > RallyState::MAX_MEMORY_SLOTS) return;
    RallyState& s = *d->state;
    s.last_memory_slot = slot_1;
    RallyState::MemorySlot& slot = s.memory_slots[slot_1 - 1];
    s.segments = slot.segments;
    s.beep_waypoints_m = deriveWaypoints(s.segments);
    d->tickMem.beepCursorsStale = true;
    adoptRoadbookIfIdle(d);
    saveAndNotify(d);
}

int stageEditAddDistances(AppData* d, const std::string& text, std::string* rest, std::string* notice) {
    STAGE_EDIT_GUARD(d, 0);
    RallyState::MemorySlot& slot = slotOf(d);
    // The start figure already set stays (owner, 2026-11-15): the first end typed is a row from it.
    // Deleting the last row already puts it back to 0 (deleteRow).
    DistanceParse pr = parseStageDistances(text, d->state->distance_unit);
    const size_t before = d->state->segments.size();
    std::string note;
    if (!pr.accepted.empty()) {
        note = isItalian(*d->state)
            ? insertSectorDistances(d->state->segments, slot.first_from_m, pr.accepted, d->state->distance_unit)
            : insertStageDistances(d->state->segments, slot.first_from_m, pr.accepted, d->state->distance_unit);
        stageEditCommit(d);
    }
    if (notice) *notice = pr.problem.empty() ? note : pr.problem;
    if (rest) *rest = pr.rest;
    // Rows made = distances that were placed (a split adds a row too).
    return static_cast<int>(d->state->segments.size() - before);
}

void stageEditSetSpeed(AppData* d, size_t row, double kph) {
    STAGE_EDIT_GUARD(d, );
    if (kph < 0.0) return;
    setRowSpeed(d->state->segments, row, kph);
    stageEditCommit(d);
}

void stageEditSetChange(AppData* d, size_t row, bool chg) {
    STAGE_EDIT_GUARD(d, );
    setRowChange(d->state->segments, row, chg, !isItalian(*d->state));   // greek and surrey: new speed change defaults to automatic
    stageEditCommit(d);
}

void stageEditSetAuto(AppData* d, size_t row, bool on) {
    STAGE_EDIT_GUARD(d, );
    setRowAuto(d->state->segments, row, on);
    stageEditCommit(d);
}

bool stageEditSetRowTo(AppData* d, size_t row, double new_to_m) {
    STAGE_EDIT_GUARD(d, false);
    if (!setRowTo(d->state->segments, slotOf(d).first_from_m, row, new_to_m)) return false;
    stageEditCommit(d);
    return true;
}

void stageEditDeleteRow(AppData* d, size_t row) {
    STAGE_EDIT_GUARD(d, );
    deleteRow(d->state->segments, slotOf(d).first_from_m, row);
    stageEditCommit(d);
}

void stageEditSetBox(AppData* d, size_t row, long box) {
    STAGE_EDIT_GUARD(d, );
    if (box < 0) return;
    if (row == 0) slotOf(d).first_number = box;
    else if (row < d->state->segments.size()) d->state->segments[row].box = box;
    else return;
    stageEditCommit(d);
}

bool stageEditSetFirst(AppData* d, long number, double from_m) {
    STAGE_EDIT_GUARD(d, false);
    RallyState::MemorySlot& slot = slotOf(d);
    if (number >= 0) slot.first_number = number;
    // The roadbook ends win: a new start only shortens (or lengthens) the first row. A start that would
    // not lie before the first end is refused and the figure is left as it was.
    const bool ok = from_m < 0.0 || setFirstFrom(d->state->segments, slot.first_from_m, from_m);
    stageEditCommit(d);
    return ok;
}

void stageEditSetStart(AppData* d, long time_s, long distance_m, std::string* notice) {
    STAGE_EDIT_GUARD(d, );
    RallyState::MemorySlot& slot = slotOf(d);
    const long new_s = time_s < 0 ? -1 : time_s;
    // A timed autostart armed for this stage follows its start time: it was
    // armed at the old time, so a new time has to re-arm it (or, if it can no
    // longer be armed, switch it off) or the box would still fire at the old one.
    const bool armed_here = d->state->auto_start_rally_time_s != 0
                         && !d->state->auto_start_early_departure
                         && !d->tickMem.autoStartTriggered
                         && d->state->auto_slot == d->state->last_memory_slot;
    const bool retime = armed_here && new_s != slot.start_time_s;
    slot.start_time_s = new_s;
    if (distance_m >= 0) slot.start_distance_m = distance_m;
    stageEditCommit(d);
    if (retime) {
        std::string err;
        if (!stageEditSetAutostart(d, true, &err)) {
            stageEditSetAutostart(d, false, nullptr);
            if (notice) *notice = "Autostart switched off: " + err;
            notifyWebStateNow(d);
        }
    }
}

// A timed start belongs to its stage: clearing that stage (or all of them) cancels it.
static void cancelTimedStartIfStageGone(AppData* d, bool all) {
    if (timedStartDiesWithStage(*d->state, all)) applyAutoStartArming(d, autoStartDisarmed());
}

void stageEditClearStage(AppData* d) {
    STAGE_EDIT_GUARD(d, );
    cancelTimedStartIfStageGone(d, false);
    slotOf(d).clear();
    d->state->segments.clear();
    stageEditCommit(d);
}

void stageEditClearAll(AppData* d) {
    STAGE_EDIT_GUARD(d, );
    cancelTimedStartIfStageGone(d, true);
    for (int i = 0; i < RallyState::MAX_MEMORY_SLOTS; i++) d->state->memory_slots[i].clear();
    d->state->segments.clear();
    d->state->last_memory_slot = 1;
    stageEditCommit(d);
}

bool stageEditSetAutostart(AppData* d, bool on, std::string* error) {
    if (!on) {
        endEarlyDepartureIfArmed(d);
        applyAutoStartArming(d, autoStartDisarmed());
        return true;
    }
    // No arming while a stage is running (the Segments tick, Navi's auto key and the phone all come
    // through here): the loaded stage is the one running, so it would only restart itself.
    if (classifyTotalReset(*d->state) == TotalResetCase::StageRunning) {
        if (error) *error = "A stage is running";
        return false;
    }
    const long tod = slotOf(d).start_time_s;
    const int64_t now_ms = getRallyTime_ms(*d->state);
    if (tod < 0) {
        if (error) *error = "Set the stage start time first.";
        return false;
    }
    int64_t target = autoStartTargetForTimeOfDay(d, tod);
    target = timedStartTarget(target, now_ms);   // a time earlier than now means tomorrow
    endEarlyDepartureIfArmed(d);
    applyAutoStartArming(d, autoStartArming(target, getAutoStartEpochMs(), false));
    return true;
}
