#include "counter_poller.h"
#include "i_counter.h"
#include <chrono>
#include <cstdio>

CounterPoller::CounterPoller() {
    for (int i = 0; i < ARRAY_SIZE; i++) {
        polls[i].time_ms = 0;
    }
    most_recent_poll = {0, 0, 0};
}

bool CounterPoller::poll(ICounter* cntr1, ICounter* cntr2, uint8_t reg) {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    
    // Don't poll more than every 5ms
    if (ms - last_poll_time_ms < MIN_POLL_INTERVAL_MS) {
        return false;
    }
    
    try {
        uint32_t val1 = cntr1->readRegister(reg);
        uint32_t val2 = cntr2->readRegister(reg);
        
        // Spurious I2C read protection:
        // 1. Reject reads that jump forward unreasonably far (e.g. 0xFFFF glitches)
        // 2. Reject reads that go BACKWARDS (counter should only increment)
        // Both indicate I2C bus errors returning corrupted data.
        if (has_previous_read) {
            bool bad1 = false, bad2 = false;
            
            if (val1 < last_good_cntr1) {
                // Counter went backwards - always invalid
                bad1 = true;
            } else if ((val1 - last_good_cntr1) > MAX_COUNTER_JUMP) {
                // Counter jumped forward too far - likely all-ones glitch
                bad1 = true;
            }
            
            if (val2 < last_good_cntr2) {
                bad2 = true;
            } else if ((val2 - last_good_cntr2) > MAX_COUNTER_JUMP) {
                bad2 = true;
            }
            
            if (bad1 || bad2) {
                fprintf(stderr, "!!! BAD I2C READ REJECTED: raw c1=%u c2=%u last_good c1=%u c2=%u%s%s\n",
                        val1, val2, last_good_cntr1, last_good_cntr2,
                        bad1 ? " [c1 BAD]" : "", bad2 ? " [c2 BAD]" : "");
                // Substitute last known good value for any bad counter
                if (bad1) val1 = last_good_cntr1;
                if (bad2) val2 = last_good_cntr2;
            }
        }
        
        // Update last known good values
        last_good_cntr1 = val1;
        last_good_cntr2 = val2;
        has_previous_read = true;
        
        // Design: "Each poll if more than 0.2 second has passed since the time in the
        // first position of the array, then the array should be push down one, the last
        // value lost, and the current value and time stored in the first array position."
        // So we only store when: first poll ever, OR >0.2s since the time in position 0.
        if (polls[0].time_ms == 0) {
            // First poll: store in first position only, no shift
            polls[0].cntr1 = val1;
            polls[0].cntr2 = val2;
            polls[0].time_ms = ms;
        } else if ((ms - polls[0].time_ms) >= 200) {
            // Shift array down one (position 9 is lost), then store current in position 0
            for (int i = ARRAY_SIZE - 1; i > 0; i--) {
                polls[i] = polls[i-1];
            }
            polls[0].cntr1 = val1;
            polls[0].cntr2 = val2;
            polls[0].time_ms = ms;
        }
        // Else: do not modify the array (discard this poll for array purposes)
        
        // Always store the actual most recent I2C read for the speed calculation.
        // Position 0 is only for shifting; speed uses most_recent_poll vs get10th().
        most_recent_poll.cntr1 = val1;
        most_recent_poll.cntr2 = val2;
        most_recent_poll.time_ms = ms;
        
        // Track how many valid entries we have
        current_index = 0;
        for (int i = 0; i < ARRAY_SIZE; i++) {
            if (polls[i].time_ms > 0) {
                current_index = i + 1;
            } else {
                break;
            }
        }
        
        last_poll_time_ms = ms;
        return true;
    } catch (...) {
        return false;
    }
}

CounterPoll CounterPoller::get10th() const {
    // Design: 10th value vs most recent poll (never position 0; position 0 is for shifting only).
    // 80% of 2s = 1600ms, 20% tolerance => 1280ms. Age = most_recent_poll.time_ms - 10th.time_ms.
    if (polls[ARRAY_SIZE - 1].time_ms > 0) {
        int64_t age_ms = most_recent_poll.time_ms - polls[ARRAY_SIZE - 1].time_ms;
        if (age_ms >= 1280) {  // 80% of 2s with 20% tolerance
            return polls[ARRAY_SIZE - 1];
        }
    }
    return {0, 0, 0};
}

CounterPoll CounterPoller::getMostRecent() const {
    // The actual most recent I2C read. Never use polls[0] for speed; position 0 is for shifting.
    return most_recent_poll;
}
