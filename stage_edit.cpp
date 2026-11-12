#include "stage_edit.h"
#include "callbacks.h"
#include "calculations.h"
#include "config_file.h"
#include "rally_state.h"
#include "sector.h"
#include "stage_rows.h"

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
    if (s.rally_type == 2 && stageIsSector(s.segments)) applySectorRules(s.segments, slot.sector_duration_s);
    s.beep_waypoints_m = deriveWaypoints(s.segments);
    slot.segments = s.segments;
    slot.beep_waypoints_m = s.beep_waypoints_m;
    slot.waypoints_recorded = true;
    d->beepCursorsStale = true;
    adoptRoadbookIfIdle(d);
    ConfigFile::save(s);
    notifyWebStateNow(d);
}

void stageEditSelect(AppData* d, int slot_1) {
    STAGE_EDIT_GUARD(d, );
    if (slot_1 < 1 || slot_1 > RallyState::MAX_MEMORY_SLOTS) return;
    RallyState& s = *d->state;
    s.last_memory_slot = slot_1;
    RallyState::MemorySlot& slot = s.memory_slots[slot_1 - 1];
    s.segments = slot.segments;
    s.beep_waypoints_m = deriveWaypoints(s.segments);
    d->beepCursorsStale = true;
    adoptRoadbookIfIdle(d);
    ConfigFile::save(s);
    notifyWebStateNow(d);
}

int stageEditAddDistances(AppData* d, const std::string& text, std::string* rest, std::string* notice) {
    STAGE_EDIT_GUARD(d, 0);
    RallyState::MemorySlot& slot = slotOf(d);
    // An empty table has no frame to keep (rows deleted one by one can leave
    // it at their old end): distances are measured from 0 again. The stage's
    // start time and distance at start are separate and stay.
    if (d->state->segments.empty()) slot.first_from_m = 0.0;
    DistanceParse pr = parseStageDistances(text, d->state->distance_km);
    const size_t before = d->state->segments.size();
    std::string note;
    if (!pr.accepted.empty()) {
        note = d->state->rally_type == 2
            ? insertSectorDistances(d->state->segments, slot.first_from_m, pr.accepted, d->state->distance_km)
            : insertStageDistances(d->state->segments, slot.first_from_m, pr.accepted, d->state->distance_km);
        stageEditCommit(d);
    }
    if (notice) *notice = note;
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
    setRowChange(d->state->segments, row, chg);
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

void stageEditSetFirst(AppData* d, long number, double from_m) {
    STAGE_EDIT_GUARD(d, );
    RallyState::MemorySlot& slot = slotOf(d);
    if (number >= 0) slot.first_number = number;
    if (from_m >= 0.0) slot.first_from_m = from_m;
    stageEditCommit(d);
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
                         && !d->autoStartTriggered
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

void stageEditClearStage(AppData* d) {
    STAGE_EDIT_GUARD(d, );
    slotOf(d).clear();
    d->state->segments.clear();
    stageEditCommit(d);
}

void stageEditClearAll(AppData* d) {
    STAGE_EDIT_GUARD(d, );
    for (int i = 0; i < RallyState::MAX_MEMORY_SLOTS; i++) d->state->memory_slots[i].clear();
    d->state->segments.clear();
    d->state->last_memory_slot = 1;
    stageEditCommit(d);
}

bool stageEditSetAutostart(AppData* d, bool on, std::string* error) {
    if (!on) {
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
    const int64_t target = autoStartTargetForTimeOfDay(d, tod);
    if (target <= now_ms) {
        if (error) *error = "That start time has already passed.";
        return false;
    }
    applyAutoStartArming(d, autoStartArming(target, getAutoStartEpochMs(), false));
    return true;
}
