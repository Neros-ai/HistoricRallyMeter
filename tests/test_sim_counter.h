#ifndef TEST_SIM_COUNTER_H
#define TEST_SIM_COUNTER_H

#include "test_framework.h"
#include "../sim_counter.h"
#include <chrono>

class TestSimCounter {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Sim Counter Tests");

        // At zero elapsed time the count equals the starting value.
        suite->addTest("readRegister returns start_count at zero elapsed", []() {
            int64_t clock = 5000;
            SimCounter c(42u, 1000.0, [&clock]() { return clock; });
            ASSERT_EQ(c.readRegister(0x07), 42u);
            return true;
        });

        // Count grows by counts_per_second * elapsed_seconds.
        suite->addTest("count grows with simulated speed", []() {
            int64_t clock = 0;
            SimCounter c(0u, 1000.0, [&clock]() { return clock; });
            clock = 1000;                      // 1.0s elapsed
            ASSERT_EQ(c.readRegister(0x07), 1000u);
            clock = 2500;                      // 2.5s elapsed
            ASSERT_EQ(c.readRegister(0x07), 2500u);
            return true;
        });

        // A real counter only increments; the sim must never go backwards.
        suite->addTest("count is monotonic non-decreasing", []() {
            int64_t clock = 0;
            SimCounter c(10u, 500.0, [&clock]() { return clock; });
            uint32_t prev = c.readRegister(0x07);
            for (int64_t t = 0; t <= 5000; t += 250) {
                clock = t;
                uint32_t v = c.readRegister(0x07);
                ASSERT_GE(v, prev);
                prev = v;
            }
            return true;
        });

        // Only register 0x07 is ever read; the sim ignores which register.
        suite->addTest("register argument is ignored", []() {
            int64_t clock = 3000;
            SimCounter c(0u, 1000.0, [&clock]() { return clock; });
            ASSERT_EQ(c.readRegister(0x07), c.readRegister(0x00));
            return true;
        });

        // Changing the rate rebases the internal clock so the displayed
        // value does not jump at the instant of the change.
        suite->addTest("setCountsPerSecond changes rate without a value jump", []() {
            int64_t clock = 0;
            SimCounter c(0u, 1000.0, [&clock]() { return clock; });
            clock = 1000;                       // 1.0s elapsed @ 1000 c/s -> 1000
            ASSERT_EQ(c.readRegister(0x07), 1000u);
            c.setCountsPerSecond(2000.0);
            ASSERT_EQ(c.readRegister(0x07), 1000u);  // no jump at the instant of change
            clock = 1500;                       // +0.5s @ 2000 c/s -> +1000
            ASSERT_EQ(c.readRegister(0x07), 2000u);
            return true;
        });

        // setPaused(true) freezes the count at its current value.
        suite->addTest("setPaused(true) freezes the count", []() {
            int64_t clock = 0;
            SimCounter c(0u, 1000.0, [&clock]() { return clock; });
            clock = 1000;
            ASSERT_EQ(c.readRegister(0x07), 1000u);
            c.setPaused(true);
            clock = 5000;                       // time passes while paused
            ASSERT_EQ(c.readRegister(0x07), 1000u);
            return true;
        });

        // setPaused(false) resumes counting from the frozen value, not from
        // wherever the unpaused clock would have reached.
        suite->addTest("setPaused(false) resumes from the frozen value", []() {
            int64_t clock = 0;
            SimCounter c(0u, 1000.0, [&clock]() { return clock; });
            clock = 1000;
            c.setPaused(true);
            clock = 5000;
            c.setPaused(false);
            clock = 5500;                       // +0.5s @ 1000 c/s -> +500
            ASSERT_EQ(c.readRegister(0x07), 1500u);
            return true;
        });

        // The default clock must be wall-clock time (system_clock), the same
        // clock domain CounterPoller::poll and getRallyTime_ms use to measure
        // elapsed time. steady_clock's epoch is unspecified (typically time
        // since boot), which would desync the sim's counter growth rate from
        // the app's own elapsed-time measurements over a long-running session.
        suite->addTest("monotonicNowMs uses wall-clock (system_clock) time", []() {
            int64_t sim_ms = SimCounter::monotonicNowMs();
            auto now = std::chrono::system_clock::now();
            int64_t wall_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            ASSERT_NEAR(sim_ms, wall_ms, 2000);  // 2s tolerance for test overhead
            return true;
        });

        // isPaused() reflects the current state.
        suite->addTest("isPaused reflects pause state", []() {
            SimCounter c(0u, 1000.0);
            ASSERT_FALSE(c.isPaused());
            c.setPaused(true);
            ASSERT_TRUE(c.isPaused());
            c.setPaused(false);
            ASSERT_FALSE(c.isPaused());
            return true;
        });

        suite->addTest("the simulated count wraps like a 32-bit counter", []() {
            // static_cast<uint32_t> of an out-of-range double is undefined
            // behaviour, not a wrap. The real LS7866C is a 32-bit counter and
            // rolls over; the sim must match rather than invoke UB.
            int64_t now = 0;
            SimCounter c(0, 1000000.0, [&now]() { return now; });
            now = 5000000;  // 5000 s at 1e6 c/s = 5e9 counts, past 2^32
            uint32_t v = c.readRegister(0x07);
            // 5e9 mod 2^32 == 5e9 - 4294967296 = 705032704
            ASSERT_EQ(v, 705032704u);
            return true;
        });

        return suite;
    }
};

#endif // TEST_SIM_COUNTER_H
