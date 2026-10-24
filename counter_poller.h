#ifndef COUNTER_POLLER_H
#define COUNTER_POLLER_H

#include <cstdint>
#include "rally_types.h"

class ICounter;

class CounterPoller {
private:
    static constexpr int ARRAY_SIZE = 10;
    CounterPoll polls[ARRAY_SIZE];   // position 0 is only for array shifting, never for speed
    CounterPoll most_recent_poll;    // actual most recent I2C read, used for speed calc
    // Nero2.0 reverse. The chips only ever count up, so reversing the car
    // cannot be read from them: instead the poller keeps a second, "virtual"
    // count that moves with the raw one but SUBTRACTS each advance while
    // reverse is engaged. Every distance (Total, Trip, segment, baselines)
    // is measured on the virtual count; speed stays on the raw one so it
    // never reads negative.
    CounterPoll most_recent_virtual;
    int64_t virt1 = 0, virt2 = 0;
    uint32_t prev_raw1 = 0, prev_raw2 = 0;
    bool virt_init = false;
    bool reverse_on = false;
    int64_t pending_shift1 = 0, pending_shift2 = 0;  // raw - virtual, applied at the first poll
    int current_index = 0;
    int64_t last_poll_time_ms = 0;
    static constexpr int MIN_POLL_INTERVAL_MS = 5;
    
    // Spurious I2C read protection: reject reads that jump too far from last known good value.
    // Max allowed jump per poll interval (~5-10ms). 1000 counts ≈ 600m at default cal,
    // which at 10ms intervals would be 216,000 km/h — far beyond any real vehicle.
    static constexpr uint32_t MAX_COUNTER_JUMP = 1000;
    uint32_t last_good_cntr1 = 0;
    uint32_t last_good_cntr2 = 0;
    bool has_previous_read = false;
    // When true (RALLY_SIM_I2C), skip the jump/backwards guard. The sim can
    // leap by thousands of counts after a VM clock step or a long GTK stall;
    // rejecting that once freezes last_good forever and the harness looks dead.
    bool spurious_check_enabled = true;
    
public:
    CounterPoller();

    void setSpuriousCheckEnabled(bool enabled) { spurious_check_enabled = enabled; }
    
    bool poll(ICounter* cntr1, ICounter* cntr2, uint8_t reg);
    
    CounterPoll get10th() const;
    // Most recent read as a distance count (virtual: falls while reversing).
    CounterPoll getMostRecent() const;
    // Most recent raw I2C read; pair this with get10th() for SPEED, never polls[0].
    CounterPoll getMostRecentRaw() const;

    // Reverse: while engaged, advances of the raw counters are subtracted
    // from the virtual (distance) count.
    void setReverse(bool on) { reverse_on = on; }
    bool reverse() const { return reverse_on; }
    // Restore raw - virtual from a saved config. Only effective before the
    // first poll; later calls are ignored.
    void setVirtualShift(int64_t s1, int64_t s2) {
        if (!virt_init) { pending_shift1 = s1; pending_shift2 = s2; }
    }
    // raw - virtual, for saving.
    int64_t virtualShift1() const {
        return virt_init ? static_cast<int64_t>(prev_raw1) - virt1 : pending_shift1; }
    int64_t virtualShift2() const {
        return virt_init ? static_cast<int64_t>(prev_raw2) - virt2 : pending_shift2; }
};

#endif // COUNTER_POLLER_H
