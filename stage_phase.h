#ifndef STAGE_PHASE_H
#define STAGE_PHASE_H

#include "rally_state.h"

// Where the stage is, worked out in one place from what the box already stores (is an autostart
// armed and which kind, is the stage complete, is a row current). Nothing new is stored and no
// figure is calculated here: the keys and the phone ask this instead of each asking their own
// question of those four values.
enum class StagePhase {
    Idle,                   // nothing armed, nothing on the gauge
    Scheduled,              // an on-the-clock autostart waits; no stage is running
    ScheduledOverRunning,   // an on-the-clock autostart waits while the last stage still runs
    EarlyCountdown,         // "at the next minute": distance already counts, the clock waits
    Running,                // a stage is under way
    Finished                // driven out or finished by a press; its figures are still on screen
};
StagePhase stagePhase(const RallyState& s);

// The stage go key reads "stage abort" only while there is a stage to abort: one running, or an
// early-departure start counting down. A finished stage reads "go" (one press starts the next
// stage), and so does a scheduled start still waiting with nothing running.
bool goKeyAborts(const RallyState& s);

// The next key. It never starts a stage, so it is dead throughout the early-departure countdown;
// otherwise it is live while a row follows, or while a press ends a surrey stage or an italian
// sector.
bool nextKeyEnabled(const RallyState& s);

// The prev key: live while there is a step to take back -- a speed change (greek, surrey), a tube
// or point press (italian), or a surrey finish press. The box and the phone both ask this.
// c1 / c2: the counters now (surrey's finish may be reopened only short of the roadbook's end).
bool prevKeyEnabled(const RallyState& s, uint64_t c1, uint64_t c2);

// The box was down across an armed autostart: at startup its time has already come. Either kind
// is dropped and the box goes to idle as an abort leaves it (Total and Trip keep their readings);
// the stage is never started late, the crew press stage go. now_rally_ms: the rally clock;
// epoch_ms: what the armed seconds count from. True when an autostart was dropped.
bool dropMissedAutoStart(RallyState& s, int64_t now_rally_ms, int64_t epoch_ms);

#endif // STAGE_PHASE_H
