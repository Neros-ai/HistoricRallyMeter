#ifndef TEST_COUNTER_POLLER_REAL_H
#define TEST_COUNTER_POLLER_REAL_H

#include "test_framework.h"
#include "mock_i2c_counter.h"
#include "../counter_poller.h"
#include "../rally_types.h"
#include <thread>
#include <chrono>

// These tests drive the REAL CounterPoller (compiled from counter_poller.cpp)
// through the ICounter interface, exercising the spurious-read rejection logic
// that the hand-duplicated TestableCounterPoller never covered.
class TestCounterPollerReal {
    static void sleepPastInterval() {
        // CounterPoller enforces a 5ms minimum between polls (it uses a real
        // wall clock internally); sleep 8ms so a second poll is always accepted.
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }

public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Counter Poller (real, via ICounter)");

        suite->addTest("Real poller reads both counters via ICounter", []() {
            CounterPoller poller;
            MockI2CCounter c1(1, 0x70), c2(1, 0x71);
            c1.setRegisterValue(0x07, 100);
            c2.setRegisterValue(0x07, 200);
            bool ok = poller.poll(&c1, &c2, 0x07);
            ASSERT_TRUE(ok);
            CounterPoll recent = poller.getMostRecent();
            ASSERT_EQ(recent.cntr1, 100u);
            ASSERT_EQ(recent.cntr2, 200u);
            return true;
        });

        suite->addTest("Backwards read is rejected (last good substituted)", []() {
            CounterPoller poller;
            MockI2CCounter c1(1, 0x70), c2(1, 0x71);
            c1.setRegisterValue(0x07, 100);
            c2.setRegisterValue(0x07, 100);
            ASSERT_TRUE(poller.poll(&c1, &c2, 0x07));   // establish last good = 100
            sleepPastInterval();
            c1.setRegisterValue(0x07, 50);              // backwards on cntr1
            ASSERT_TRUE(poller.poll(&c1, &c2, 0x07));
            CounterPoll recent = poller.getMostRecent();
            ASSERT_EQ(recent.cntr1, 100u);              // substituted, not 50
            return true;
        });

        suite->addTest("Forward jump beyond MAX_COUNTER_JUMP is rejected", []() {
            CounterPoller poller;
            MockI2CCounter c1(1, 0x70), c2(1, 0x71);
            c1.setRegisterValue(0x07, 100);
            c2.setRegisterValue(0x07, 100);
            ASSERT_TRUE(poller.poll(&c1, &c2, 0x07));
            sleepPastInterval();
            c1.setRegisterValue(0x07, 100 + 2000);      // jump 2000 > 1000 cap
            ASSERT_TRUE(poller.poll(&c1, &c2, 0x07));
            CounterPoll recent = poller.getMostRecent();
            ASSERT_EQ(recent.cntr1, 100u);              // substituted, not 2100
            return true;
        });

        suite->addTest("Forward jump within cap is accepted", []() {
            CounterPoller poller;
            MockI2CCounter c1(1, 0x70), c2(1, 0x71);
            c1.setRegisterValue(0x07, 100);
            c2.setRegisterValue(0x07, 100);
            ASSERT_TRUE(poller.poll(&c1, &c2, 0x07));
            sleepPastInterval();
            c1.setRegisterValue(0x07, 150);             // jump 50 < 1000
            c2.setRegisterValue(0x07, 150);
            ASSERT_TRUE(poller.poll(&c1, &c2, 0x07));
            CounterPoll recent = poller.getMostRecent();
            ASSERT_EQ(recent.cntr1, 150u);
            return true;
        });

        suite->addTest("Read failure makes poll return false", []() {
            CounterPoller poller;
            MockI2CCounter c1(1, 0x70), c2(1, 0x71);
            c1.setFailMode(true);                       // readRegister throws
            ASSERT_FALSE(poller.poll(&c1, &c2, 0x07));
            return true;
        });

        return suite;
    }
};

#endif // TEST_COUNTER_POLLER_REAL_H
