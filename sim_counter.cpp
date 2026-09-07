#include "sim_counter.h"
#include <cmath>
#include <chrono>
#include <utility>

int64_t SimCounter::monotonicNowMs() {
    // system_clock, not steady_clock: CounterPoller::poll and getRallyTime_ms
    // both timestamp against system_clock, so the simulated counter's growth
    // must be driven by the same clock domain -- otherwise the two can drift
    // apart (e.g. under VM clock skew during a long-running session) and the
    // computed speed/ahead-behind values drift with them.
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
}

SimCounter::SimCounter(uint32_t start_count_,
                       double counts_per_second_,
                       std::function<int64_t()> now_ms_fn)
    : start_count(start_count_),
      counts_per_second(counts_per_second_),
      now_ms(std::move(now_ms_fn)) {
    start_ms = now_ms();
}

uint32_t SimCounter::readRegister(uint8_t reg) {
    (void)reg;  // Only register 0x07 is read; the sim ignores which one.
    if (paused) return start_count;
    int64_t elapsed_ms = now_ms() - start_ms;
    if (elapsed_ms < 0) elapsed_ms = 0;  // guard against a non-monotonic clock
    double grown = counts_per_second * (static_cast<double>(elapsed_ms) / 1000.0);
    // Casting an out-of-range double to uint32_t is undefined behaviour, not
    // a wrap. The real LS7866C is a 32-bit counter and rolls over, so fold
    // the value into range first and let the sim roll over the same way.
    grown = std::fmod(grown, 4294967296.0);
    if (grown < 0.0) grown = 0.0;
    return start_count + static_cast<uint32_t>(grown);
}

void SimCounter::setCountsPerSecond(double counts_per_second_) {
    // Rebase at the current value/time so the rate change does not cause a
    // jump on the next read.
    start_count = readRegister(0);
    start_ms = now_ms();
    counts_per_second = counts_per_second_;
}

void SimCounter::setPaused(bool paused_) {
    if (paused_ == paused) return;
    start_count = readRegister(0);
    start_ms = now_ms();
    paused = paused_;
}
