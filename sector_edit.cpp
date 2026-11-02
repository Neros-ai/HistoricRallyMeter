#include "sector_edit.h"
#include "callbacks.h"
#include "rally_state.h"
#include "sector.h"
#include "stage_edit.h"
#include <cmath>

void sectorEditStepType(AppData* d, size_t row) {
    if (stageEditLocked(*d->state)) return;
    std::vector<Segment>& segs = d->state->segments;
    if (row >= segs.size()) return;
    Segment& s = segs[row];
    const double secs = rowSeconds(s);
    s.type = stepSegmentType(s.type == SEG_LEG ? SEG_CO : s.type);
    std::vector<Segment> others;
    for (size_t i = 0; i < segs.size(); i++) if (i != row) others.push_back(segs[i]);
    s.ref = (s.type == SEG_NAVI) ? 0 : nextRef(others, s.type);
    if (s.type == SEG_PC) s.pc_duration_s = std::round(secs);
    stageEditCommit(d);
}

void sectorEditSetRef(AppData* d, size_t row, int ref) {
    if (stageEditLocked(*d->state) || row >= d->state->segments.size() || ref < 0) return;
    d->state->segments[row].ref = ref;
    stageEditCommit(d);
}

void sectorEditSetPcDuration(AppData* d, size_t row, double seconds) {
    if (stageEditLocked(*d->state) || row >= d->state->segments.size() || seconds <= 0.0) return;
    Segment& s = d->state->segments[row];
    if (s.type != SEG_PC) return;
    s.pc_duration_s = std::round(seconds);
    stageEditCommit(d);   // the rules work the speed out
}

void sectorEditSetSectorDuration(AppData* d, long seconds) {
    if (stageEditLocked(*d->state)) return;
    d->state->memory_slots[stageEditSlot(d) - 1].sector_duration_s = seconds > 0 ? seconds : -1;
    stageEditCommit(d);
}
