#include "stage_phase.h"
#include "calculations.h"
#include "sector.h"
#include "surrey.h"

StagePhase stagePhase(const RallyState& s) {
    const bool armed = s.auto_start_rally_time_s != 0;
    if (armed && s.auto_start_early_departure) return StagePhase::EarlyCountdown;
    const bool on_gauge = s.segment_current_number >= 0;
    const bool running = on_gauge && !s.stage_complete;
    if (armed) return running ? StagePhase::ScheduledOverRunning : StagePhase::Scheduled;
    if (running) return StagePhase::Running;
    return on_gauge ? StagePhase::Finished : StagePhase::Idle;
}

bool goKeyAborts(const RallyState& s) {
    switch (stagePhase(s)) {
    case StagePhase::Running:
    case StagePhase::ScheduledOverRunning:
    case StagePhase::EarlyCountdown:
        return true;
    case StagePhase::Idle:
    case StagePhase::Scheduled:
    case StagePhase::Finished:
        return false;
    }
    return false;
}

bool nextKeyEnabled(const RallyState& s) {
    if (stagePhase(s) == StagePhase::EarlyCountdown) return false;
    return nextAvailable(s) || surreyFinishAvailable(s) || sectorFinishAvailable(s);
}

bool prevKeyEnabled(const RallyState& s, uint64_t c1, uint64_t c2) {
    // A speed change is taken back only while the stage runs; a finished stage keeps prev for a
    // pressed surrey finish alone.
    return (!s.stage_complete && prevAvailable(s)) || sectorUndoAvailable(s) ||
           surreyUndoAvailableNow(s, c1, c2);
}

bool dropMissedAutoStart(RallyState& s, int64_t now_rally_ms, int64_t epoch_ms) {
    if (s.auto_start_rally_time_s == 0) return false;
    if (now_rally_ms < autoStartTargetMsFromSeconds(s.auto_start_rally_time_s, epoch_ms)) return false;
    // As performAbortStage leaves the box: nothing armed, no stage, no pc error carried over.
    s.auto_start_rally_time_s = 0;
    s.auto_start_early_departure = false;
    s.segment_current_number = -1;
    s.stage_complete = true;
    s.last_pc_error = RallyState::PcError();
    return true;
}
