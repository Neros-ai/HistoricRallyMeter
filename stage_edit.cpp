#include "stage_edit.h"
#include "callbacks.h"
#include "calculations.h"
#include "config_file.h"
#include "rally_state.h"
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

static RallyState::MemorySlot& slotOf(AppData* d) {
    return d->state->memory_slots[stageEditSlot(d) - 1];
}

void stageEditCommit(AppData* d) {
    RallyState& s = *d->state;
    RallyState::MemorySlot& slot = slotOf(d);
    recomputeSegmentCounts(s.segments, s.calibration);
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

int stageEditAddDistances(AppData* d, const std::string& text, std::string* rest) {
    RallyState::MemorySlot& slot = slotOf(d);
    double end = slot.first_from_m;
    for (const Segment& sg : d->state->segments) end += sg.distance_m;
    DistanceParse pr = parseStageDistances(text, end);
    if (!pr.accepted.empty()) {
        appendStageDistances(d->state->segments, slot.first_from_m, pr.accepted);
        stageEditCommit(d);
    }
    if (rest) *rest = pr.rest;
    return static_cast<int>(pr.accepted.size());
}

void stageEditSetSpeed(AppData* d, size_t row, double kph) {
    if (kph < 0.0) return;
    setRowSpeed(d->state->segments, row, kph);
    stageEditCommit(d);
}

void stageEditSetChange(AppData* d, size_t row, bool chg) {
    setRowChange(d->state->segments, row, chg);
    stageEditCommit(d);
}

void stageEditSetAuto(AppData* d, size_t row, bool on) {
    setRowAuto(d->state->segments, row, on);
    stageEditCommit(d);
}

void stageEditDeleteRow(AppData* d, size_t row) {
    deleteRow(d->state->segments, slotOf(d).first_from_m, row);
    stageEditCommit(d);
}

void stageEditSetFirst(AppData* d, long number, double from_m) {
    RallyState::MemorySlot& slot = slotOf(d);
    if (number >= 0) slot.first_number = number;
    if (from_m >= 0.0) slot.first_from_m = from_m;
    stageEditCommit(d);
}

void stageEditSetStart(AppData* d, long time_s, long distance_m) {
    RallyState::MemorySlot& slot = slotOf(d);
    slot.start_time_s = time_s < 0 ? -1 : time_s;
    if (distance_m >= 0) slot.start_distance_m = distance_m;
    stageEditCommit(d);
}

void stageEditClearStage(AppData* d) {
    slotOf(d).clear();
    d->state->segments.clear();
    stageEditCommit(d);
}

void stageEditClearAll(AppData* d) {
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
