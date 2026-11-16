#include "stage_store.h"
#include "calculations.h"
#include "stage_rows.h"
#include "sector.h"
#include "surrey.h"

bool validStageNumber(int n) { return n >= 1 && n <= RallyState::MAX_MEMORY_SLOTS; }

std::vector<int> programmedStages(const RallyState& s) {
    std::vector<int> out;
    for (int i = 0; i < RallyState::MAX_MEMORY_SLOTS; i++)
        if (!s.memory_slots[i].empty()) out.push_back(i + 1);
    return out;
}

int chosenStageNumber(const std::string& text) {
    if (text.empty() || text.size() > 2) return 0;
    int n = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return 0;
        n = n * 10 + (c - '0');
    }
    return validStageNumber(n) ? n : 0;
}

bool unitChangeRescalesStages(int old_unit, int new_unit) {
    // Metres counts as km: m <-> miles keeps the figures as km <-> miles does (owner, 2026-11-15).
    return distUnitValid(old_unit) && distUnitValid(new_unit) && distIsMiles(old_unit) != distIsMiles(new_unit);
}

bool changeDistanceUnit(RallyState& s, int new_unit) {
    const int old_unit = s.distance_unit;
    if (!unitChangeRescalesStages(old_unit, new_unit)) { s.distance_unit = new_unit; return true; }
    if (stageLive(s)) return false;   // never rescale a stage under the car
    // The rows on screen are a copy of the stage being edited: carry them over from that stage's own start figure.
    double live_from = (s.last_memory_slot >= 1 && s.last_memory_slot <= RallyState::MAX_MEMORY_SLOTS)
                           ? s.memory_slots[s.last_memory_slot - 1].first_from_m : 0.0;
    long unused_start = 0;
    reinterpretStageForUnit(s.segments, live_from, unused_start, old_unit, new_unit);
    for (int i = 0; i < RallyState::MAX_MEMORY_SLOTS; i++) {
        RallyState::MemorySlot& slot = s.memory_slots[i];
        if (slot.empty() && slot.start_distance_m == 0 && slot.first_from_m == 0.0) continue;
        reinterpretStageForUnit(slot.segments, slot.first_from_m, slot.start_distance_m, old_unit, new_unit);
        if (!slot.segments.empty()) slot.beep_waypoints_m = deriveWaypoints(slot.segments);
    }
    // Italian: a pc's speed is its distance over its duration, and the navi speed comes from the sector duration, so
    // both follow the new distances -- the same row rules every stage edit applies (stageEditCommit).
    if (isItalian(s)) {
        for (int i = 0; i < RallyState::MAX_MEMORY_SLOTS; i++) {
            RallyState::MemorySlot& slot = s.memory_slots[i];
            if (stageIsSector(slot.segments)) applySectorRules(slot.segments, slot.sector_duration_s);
        }
        if (stageIsSector(s.segments))
            applySectorRules(s.segments, (s.last_memory_slot >= 1 && s.last_memory_slot <= RallyState::MAX_MEMORY_SLOTS)
                                             ? s.memory_slots[s.last_memory_slot - 1].sector_duration_s : 0);
    }
    s.beep_waypoints_m = deriveWaypoints(s.segments);
    s.distance_unit = new_unit;
    return true;
}
