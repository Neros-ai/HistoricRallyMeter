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
    
public:
    CounterPoller();
    
    bool poll(ICounter* cntr1, ICounter* cntr2, uint8_t reg);
    
    CounterPoll get10th() const;
    CounterPoll getMostRecent() const;  // most recent I2C read; use this for speed, never polls[0]
};

#endif // COUNTER_POLLER_H
