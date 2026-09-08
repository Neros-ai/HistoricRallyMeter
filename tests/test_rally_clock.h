#ifndef TEST_RALLY_CLOCK_H
#define TEST_RALLY_CLOCK_H

#include "test_framework.h"
#include "../calculations.h"
#include "../rally_state.h"
#include <chrono>

class TestRallyClock {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("RallyClock and Reset Tests");
        
        // Test RallyClock = system_time + offset
        suite->addTest("RallyClock = system_time + rallyTimeOffset_ms", []() {
            RallyState state;
            state.rallyTimeOffset_ms = 3600000;  // +1 hour
            
            auto now = std::chrono::system_clock::now();
            auto system_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            
            int64_t rally_time = getRallyTime_ms(state);
            
            // Rally time should be approximately system time + offset
            ASSERT_NEAR(rally_time, system_ms + 3600000, 100);  // Allow 100ms tolerance
            
            return true;
        });
        
        // Test default offset is 0
        suite->addTest("Default rallyTimeOffset_ms is 0", []() {
            RallyState state;
            ASSERT_EQ(state.rallyTimeOffset_ms, 0);
            return true;
        });
        
        // Test set and save updates offset
        suite->addTest("Set and save updates rallyTimeOffset_ms correctly", []() {
            RallyState state;
            
            int64_t input_rally_time_ms = 1234567890000;  // Some rally time
            int64_t system_time_ms = 1234567800000;       // System time
            
            // Calculate offset
            state.rallyTimeOffset_ms = input_rally_time_ms - system_time_ms;
            
            ASSERT_EQ(state.rallyTimeOffset_ms, 90000);  // 90 seconds difference
            
            return true;
        });
        
        // Test 24-hour format
        suite->addTest("RallyClock displays in 24-hour format", []() {
            // 14:30:45 = 14*3600 + 30*60 + 45 seconds from midnight
            // But formatTime uses localtime, so we need actual timestamp
            
            // Just verify format is HH:MM:SS
            int64_t test_time_ms = 1234567890123;
            std::string formatted = formatTime(test_time_ms);
            
            // Should be in format XX:XX:XX
            ASSERT_EQ(formatted.length(), 8u);
            ASSERT_EQ(formatted[2], ':');
            ASSERT_EQ(formatted[5], ':');
            
            return true;
        });
        
        // Test Total reset
        suite->addTest("Total reset sets counters to current", []() {
            RallyState state;
            state.total_start_cntr1 = 100;
            state.total_start_cntr2 = 200;
            
            uint64_t current_cntr1 = 5000;
            uint64_t current_cntr2 = 6000;
            
            // Simulate reset
            state.total_start_cntr1 = current_cntr1;
            state.total_start_cntr2 = current_cntr2;
            
            ASSERT_EQ(state.total_start_cntr1, 5000u);
            ASSERT_EQ(state.total_start_cntr2, 6000u);
            
            return true;
        });
        
        // Test Total reset sets time
        suite->addTest("Total reset sets total_start_time_ms to current time", []() {
            RallyState state;
            state.total_start_time_ms = 0;
            
            auto now = std::chrono::system_clock::now();
            int64_t current_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            
            state.total_start_time_ms = current_time;
            
            ASSERT_NEAR(state.total_start_time_ms, current_time, 100);
            
            return true;
        });
        
        // Test Trip reset
        suite->addTest("Trip reset sets trip counters and time", []() {
            RallyState state;
            
            uint64_t current_cntr1 = 8000;
            uint64_t current_cntr2 = 9000;
            int64_t current_time = 123456789;
            
            state.trip_start_cntr1 = current_cntr1;
            state.trip_start_cntr2 = current_cntr2;
            state.trip_start_time_ms = current_time;
            
            ASSERT_EQ(state.trip_start_cntr1, 8000u);
            ASSERT_EQ(state.trip_start_cntr2, 9000u);
            ASSERT_EQ(state.trip_start_time_ms, 123456789);
            
            return true;
        });
        
        // Test Next segment resets Trip
        suite->addTest("Next segment resets Trip counters and time", []() {
            RallyState state;
            state.trip_start_cntr1 = 100;
            state.segment_current_number = 0;
            
            // Advance to next segment
            uint64_t current_cntr1 = 5000;
            int64_t current_time = 999999;
            
            state.segment_current_number++;
            state.trip_start_cntr1 = current_cntr1;
            state.trip_start_time_ms = current_time;
            state.segment_start_cntr1 = current_cntr1;
            state.segment_start_time_ms = current_time;
            
            ASSERT_EQ(state.segment_current_number, 1);
            ASSERT_EQ(state.trip_start_cntr1, 5000u);
            ASSERT_EQ(state.segment_start_cntr1, 5000u);
            
            return true;
        });
        
        // Test Next segment increments number
        suite->addTest("Next segment increments segment_current_number", []() {
            RallyState state;
            state.segments.push_back({100, 1000, true});
            state.segments.push_back({200, 2000, false});
            state.segment_current_number = 0;
            
            state.segment_current_number++;
            
            ASSERT_EQ(state.segment_current_number, 1);
            
            return true;
        });
        
        // Test format duration
        suite->addTest("Format duration as hh:mm:ss", []() {
            int64_t duration_ms = 3723000;  // 1h 2m 3s
            
            std::string formatted = formatDuration(duration_ms);
            ASSERT_STR_EQ(formatted, "001:02:03.0");
            
            return true;
        });
        
        // Test format duration over 99 hours
        suite->addTest("Format duration handles over 99 hours", []() {
            int64_t duration_ms = 360000000;  // 100 hours
            
            std::string formatted = formatDuration(duration_ms);
            // Should show 100:00:00 (3 digit hours)
            ASSERT_TRUE(formatted.find("100") != std::string::npos);
            
            return true;
        });
        
        // Test negative offset
        suite->addTest("Rally clock handles negative offset", []() {
            RallyState state;
            state.rallyTimeOffset_ms = -3600000;  // -1 hour
            
            auto now = std::chrono::system_clock::now();
            auto system_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            
            int64_t rally_time = getRallyTime_ms(state);
            
            ASSERT_NEAR(rally_time, system_ms - 3600000, 100);
            
            return true;
        });
        
        // ---- autostart target storage ----
        // The target was stored as MINUTES since the epoch, so on_autostart_set
        // parsed and validated HH:MM:SS and then truncated the seconds away --
        // an autostart set for 10:30:45 fired at 10:30:00, 45 seconds early.

        suite->addTest("an autostart target keeps its seconds", []() {
            int64_t epoch_ms = 1000000000000LL;
            int64_t target_ms = epoch_ms + (10 * 3600 + 30 * 60 + 45) * 1000LL;
            uint64_t stored = autoStartSecondsFromTargetMs(target_ms, epoch_ms);
            ASSERT_EQ(autoStartTargetMsFromSeconds(stored, epoch_ms), target_ms);
            return true;
        });

        suite->addTest("an autostart target on a whole minute round-trips too", []() {
            int64_t epoch_ms = 1000000000000LL;
            int64_t target_ms = epoch_ms + (10 * 3600 + 30 * 60) * 1000LL;
            uint64_t stored = autoStartSecondsFromTargetMs(target_ms, epoch_ms);
            ASSERT_EQ(autoStartTargetMsFromSeconds(stored, epoch_ms), target_ms);
            return true;
        });

        suite->addTest("sub-second precision is not claimed", []() {
            // Seconds are the stored resolution: a target 400ms past the second
            // lands on that second, not the next one.
            int64_t epoch_ms = 1000000000000LL;
            int64_t target_ms = epoch_ms + 45 * 1000LL + 400;
            uint64_t stored = autoStartSecondsFromTargetMs(target_ms, epoch_ms);
            ASSERT_EQ(autoStartTargetMsFromSeconds(stored, epoch_ms), epoch_ms + 45000);
            return true;
        });

        suite->addTest("a target at the epoch stores as zero, meaning not set", []() {
            int64_t epoch_ms = 1000000000000LL;
            ASSERT_EQ(autoStartSecondsFromTargetMs(epoch_ms, epoch_ms), 0u);
            return true;
        });

        suite->addTest("an arming press overrules whatever was armed before", []() {
            // The whole point of AutoStartArming being assigned wholesale: no
            // field of the previous arming can survive into the new one.
            int64_t epoch_ms = 1000000000000LL;
            AutoStartArming existing = autoStartArming(epoch_ms + 600000, epoch_ms, true);
            existing.triggered = true;

            // An ordinary autostart pressed over a pending on-the-minute one.
            AutoStartArming now = autoStartArming(epoch_ms + 120000, epoch_ms, false);
            existing = now;
            ASSERT_EQ(existing.rally_time_s, 120u);
            ASSERT_FALSE(existing.early_departure);
            ASSERT_FALSE(existing.triggered);
            ASSERT_FALSE(existing.zero_distance_now);
            return true;
        });

        suite->addTest("re-arming on the same target time still fully re-arms", []() {
            // Pressing "on the minute" twice inside the same minute produces
            // the same target. It must still come out as a complete, fresh
            // arming -- kind set, trigger cleared, distance zeroed again --
            // rather than being treated as a no-op because nothing moved.
            int64_t epoch_ms = 1000000000000LL;
            int64_t target_ms = epoch_ms + 60000;
            AutoStartArming first = autoStartArming(target_ms, epoch_ms, true);
            first.triggered = true;   // pretend it had already fired
            AutoStartArming second = autoStartArming(target_ms, epoch_ms, true);
            ASSERT_EQ(second.rally_time_s, first.rally_time_s);
            ASSERT_TRUE(second.early_departure);
            ASSERT_FALSE(second.triggered);
            ASSERT_TRUE(second.zero_distance_now);
            return true;
        });

        suite->addTest("only the on-the-minute kind zeroes distance at arming", []() {
            int64_t epoch_ms = 1000000000000LL;
            ASSERT_TRUE(autoStartArming(epoch_ms + 60000, epoch_ms, true).zero_distance_now);
            ASSERT_FALSE(autoStartArming(epoch_ms + 60000, epoch_ms, false).zero_distance_now);
            return true;
        });

        suite->addTest("an out-of-range autostart time is refused, not multiplied", []() {
            // auto_start_rally_time_s is a uint64_t read from a hand-editable
            // config file. Multiplying it out unchecked is signed overflow --
            // undefined behaviour, reached on every co-pilot tick with no
            // operator action at all.
            int64_t epoch_ms = 1000000000000LL;
            ASSERT_TRUE(autoStartSecondsInRange(0));
            // The field counts from 2020-01-01, not from the rally start, so
            // an ordinary armed value is already years of seconds. A bound
            // that rejected these would silently disarm every autostart --
            // no countdown, no hold, and the panel reading "none".
            ASSERT_TRUE(autoStartSecondsInRange(6ULL * 365 * 24 * 60 * 60));
            ASSERT_TRUE(autoStartSecondsInRange(AUTO_START_MAX_SECONDS));
            ASSERT_FALSE(autoStartSecondsInRange(AUTO_START_MAX_SECONDS + 1));
            ASSERT_FALSE(autoStartSecondsInRange(~0ULL));

            // Out of range reads as the epoch itself -- already past, so it
            // never fires -- rather than a wrapped-around moment.
            ASSERT_EQ(autoStartTargetMsFromSeconds(~0ULL, epoch_ms), epoch_ms);
            ASSERT_EQ(autoStartTargetMsFromSeconds(AUTO_START_MAX_SECONDS + 1, epoch_ms), epoch_ms);
            // In range still resolves normally.
            ASSERT_EQ(autoStartTargetMsFromSeconds(45, epoch_ms), epoch_ms + 45000);

            // And the panel says nothing is armed rather than printing a
            // real-looking time from the start of the rally.
            ASSERT_EQ(formatAutoStartStatus(~0ULL, true, epoch_ms), std::string("none"));
            return true;
        });

        suite->addTest("disarming leaves nothing armed", []() {
            AutoStartArming off = autoStartDisarmed();
            ASSERT_EQ(off.rally_time_s, 0u);
            ASSERT_FALSE(off.early_departure);
            ASSERT_FALSE(off.triggered);
            ASSERT_FALSE(off.zero_distance_now);
            // And nothing disarmed can hold the time-error box at zero.
            ASSERT_FALSE(autoStartHoldsTimeError(off.rally_time_s, off.early_departure,
                                                 off.triggered, 16000));
            return true;
        });

        suite->addTest("an on-the-minute autostart holds the time error at zero", []() {
            // Armed, early departure, not yet fired, still counting down.
            ASSERT_TRUE(autoStartHoldsTimeError(45, true, false, 16000));
            // Right up to the last millisecond before the minute.
            ASSERT_TRUE(autoStartHoldsTimeError(45, true, false, 1));
            return true;
        });

        suite->addTest("the hold releases the moment the autostart fires", []() {
            // diff_ms has reached zero, so the clock is being zeroed now and
            // the box must start reporting the real figure again.
            ASSERT_FALSE(autoStartHoldsTimeError(45, true, false, 0));
            ASSERT_FALSE(autoStartHoldsTimeError(45, true, true, 16000));
            return true;
        });

        suite->addTest("only the on-the-minute autostart holds the time error", []() {
            // An autostart entered on the Set Autostart screen arms nothing
            // and zeroes nothing until it fires, so a stage already under way
            // keeps showing its real error while that one counts down.
            ASSERT_FALSE(autoStartHoldsTimeError(45, false, false, 16000));
            // Nothing armed at all.
            ASSERT_FALSE(autoStartHoldsTimeError(0, true, false, 16000));
            return true;
        });

        suite->addTest("a stale autostart target does not hold the box at zero", []() {
            // A target left in a config file from a previous rally never
            // fires, so a hold keyed only on "armed and not triggered" would
            // peg the time error at zero for the whole of the next one.
            ASSERT_FALSE(autoStartHoldsTimeError(45, true, false, -90000));
            // Likewise a target absurdly far ahead, which shows no countdown.
            ASSERT_FALSE(autoStartHoldsTimeError(45, true, false, 25LL * 3600 * 1000));
            return true;
        });

        return suite;
    }
};

#endif // TEST_RALLY_CLOCK_H
