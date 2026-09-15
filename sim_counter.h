#ifndef SIM_COUNTER_H
#define SIM_COUNTER_H

#include <cstdint>
#include <functional>
#include "i_counter.h"

// Simulated counter for off-target development (no /dev/i2c-1 present).
// Synthesises a monotonically increasing 32-bit count as though the vehicle
// were moving at a fixed rate, so the full GUI can start and run on a dev
// machine. The clock is injectable so tests are deterministic; production
// uses a real monotonic millisecond clock.
class SimCounter : public ICounter {
private:
    uint32_t start_count;
    double counts_per_second;
    std::function<int64_t()> now_ms;
    int64_t start_ms;
    bool paused = false;

public:
    // now_ms_fn returns a monotonic timestamp in milliseconds. Defaults to a
    // real monotonic clock when omitted.
    SimCounter(uint32_t start_count_,
               double counts_per_second_,
               std::function<int64_t()> now_ms_fn = SimCounter::monotonicNowMs);

    uint32_t readRegister(uint8_t reg) override;

    // Change the simulated rate. Rebases the internal start point to the
    // current value/time so the next readRegister() does not jump.
    void setCountsPerSecond(double counts_per_second_);

    // Pause/resume the simulated count. While paused, readRegister() returns
    // a frozen value; resuming rebases so counting continues from there.
    void setPaused(bool paused_);

    bool isPaused() const { return paused; }

    static int64_t monotonicNowMs();
};

#endif // SIM_COUNTER_H
