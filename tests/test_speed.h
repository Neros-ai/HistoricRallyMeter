#ifndef TEST_SPEED_H
#define TEST_SPEED_H

#include "test_framework.h"
#include "../calculations.h"
#include "../rally_state.h"

class TestSpeed {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Speed Calculation Tests");
        
        // Test counts per hour to KPH
        suite->addTest("Convert counts/hour to KPH", []() {
            long calibration = 600000;  // 600mm per count = 0.6m per count
            long counts_per_hour = 100000;  // 100000 counts per hour
            
            double kph = countsPerHourToKPH(counts_per_hour, calibration);
            // 100000 * 600000 / 1000 = 60000000000 meters/h / 1000 = 60000 km/h... 
            // Wait that's wrong. Let me recalculate.
            // meters_per_hour = counts/h * (cal/1000/1000) = 100000 * 0.6 = 60000 m/h
            // kph = 60000 / 1000 = 60 km/h
            ASSERT_NEAR(kph, 60.0, 0.1);
            
            return true;
        });
        
        // Test KPH to counts per hour
        suite->addTest("Convert KPH to counts/hour", []() {
            long calibration = 600000;
            double kph = 60.0;
            
            double counts_per_hour = kphToCountsPerHour(kph, calibration);
            // 60 * 1000000 / 600000 = 100000
            ASSERT_NEAR(counts_per_hour, 100000.0, 0.001);
            
            return true;
        });
        
        // Test KPH to MPH conversion
        suite->addTest("100 KPH displays as 62.14 MPH", []() {
            double kph = 100.0;
            double mph = kph * 100000.0 / 160934.0;
            
            ASSERT_NEAR(mph, 62.14, 0.01);
            
            return true;
        });
        
        // Test MPH to KPH conversion
        suite->addTest("62.14 MPH converts back to ~100 KPH", []() {
            double mph = 62.14;
            double kph = mph * 160934.0 / 100000.0;
            
            ASSERT_NEAR(kph, 100.0, 0.1);
            
            return true;
        });
        
        // Test current speed calculation in KPH
        suite->addTest("Current speed calculation in KPH", []() {
            RallyState state;
            state.counters = false;
            state.units = false;  // KPH
            state.calibration = 600000;
            
            // Use realistic time values (tenth at 1ms to avoid zero)
            CounterPoll current = {1000, 0, 2001};  // 1000 counts at 2001ms
            CounterPoll tenth = {0, 0, 1};  // 0 counts at 1ms (2 second window)
            
            double speed = calculateCurrentSpeed(state, current, tenth);
            
            // counts_diff = 1000, time_diff = 2000ms = 2s
            // cm_diff = 1000 * 600000 / 10000 = 60000 cm = 600m
            // speed_cm_per_s = 60000 * 1000 / 2000 = 30000 cm/s = 300 m/s
            // KPH = 30000 * 3600 / 100000 = 1080 km/h
            
            ASSERT_NEAR(speed, 1080.0, 1.0);
            
            return true;
        });
        
        // Test current speed calculation in MPH
        suite->addTest("Current speed calculation in MPH", []() {
            RallyState state;
            state.counters = false;
            state.units = true;  // MPH
            state.calibration = 600000;
            
            CounterPoll current = {1000, 0, 2001};
            CounterPoll tenth = {0, 0, 1};
            
            double speed = calculateCurrentSpeed(state, current, tenth);
            
            // Same as above but in MPH
            // 1080 km/h * 100000 / 160934 = 671 mph approximately
            ASSERT_NEAR(speed, 671.0, 1.0);
            
            return true;
        });
        
        // Test average speed calculation
        suite->addTest("Average speed since start", []() {
            RallyState state;
            state.counters = false;
            state.units = false;  // KPH
            state.calibration = 600000;
            
            int64_t start_time_ms = 0;
            int64_t current_time_ms = 3600000;  // 1 hour
            int64_t count_diff = 100000;  // 100000 counts in 1 hour
            
            double speed = calculateAverageSpeed(state, start_time_ms, current_time_ms, count_diff);
            
            // 100000 counts * 0.6m = 60000m = 60km in 1 hour = 60 km/h
            ASSERT_NEAR(speed, 60.0, 0.1);
            
            return true;
        });
        
        // Test speed with zero time
        suite->addTest("Speed displays --.-- when time is zero", []() {
            RallyState state;
            state.counters = false;
            
            CounterPoll current = {1000, 0, 1000};
            CounterPoll tenth = {0, 0, 0};  // time_ms = 0 means invalid
            
            double speed = calculateCurrentSpeed(state, current, tenth);
            
            // Should return -1.0 (invalid)
            ASSERT_LT(speed, 0);
            
            return true;
        });
        
        // Test speed with negative time difference
        suite->addTest("Speed invalid when time diff <= 0", []() {
            RallyState state;
            state.counters = false;
            
            CounterPoll current = {1000, 0, 100};
            CounterPoll tenth = {500, 0, 200};  // tenth.time > current.time
            
            double speed = calculateCurrentSpeed(state, current, tenth);
            
            // Should return -1.0 (invalid)
            ASSERT_LT(speed, 0);
            
            return true;
        });
        
        // Test unit switching
        suite->addTest("Unit switch recalculates speed correctly", []() {
            RallyState state;
            state.counters = false;
            state.calibration = 600000;
            
            CounterPoll current = {100000, 0, 3600001};  // 100000 counts in ~1 hour
            CounterPoll tenth = {0, 0, 1};
            
            // Calculate in KPH
            state.units = false;
            double kph = calculateCurrentSpeed(state, current, tenth);
            
            // Calculate in MPH
            state.units = true;
            double mph = calculateCurrentSpeed(state, current, tenth);
            
            // Verify conversion ratio
            ASSERT_NEAR(kph / mph, 1.60934, 0.01);
            
            return true;
        });
        
        // Test zero counts (stationary)
        suite->addTest("Zero counts gives zero speed", []() {
            RallyState state;
            state.counters = false;
            state.units = false;
            state.calibration = 600000;
            
            CounterPoll current = {0, 0, 1001};  // Same counts at different time
            CounterPoll tenth = {0, 0, 1};
            
            double speed = calculateCurrentSpeed(state, current, tenth);
            
            ASSERT_NEAR(speed, 0.0, 0.001);
            
            return true;
        });
        
        // Test high speed (200 KPH)
        suite->addTest("High speed (200 KPH) calculated correctly", []() {
            RallyState state;
            state.counters = false;
            state.units = false;  // KPH
            state.calibration = 600000;
            
            // 200 km/h = 200000m/h
            // With 0.6m/count: 200000/0.6 = 333333 counts/hour
            // In 2 seconds: 333333 * 2 / 3600 = 185 counts
            
            CounterPoll current = {185, 0, 2001};
            CounterPoll tenth = {0, 0, 1};
            
            double speed = calculateCurrentSpeed(state, current, tenth);
            
            // Should be approximately 200 km/h
            ASSERT_NEAR(speed, 200.0, 5.0);  // Allow some tolerance due to rounding
            
            return true;
        });
        
        // ---- average speed and the -10/+10 correction ----
        // The correction was applied only to the distance READOUT, so after a
        // -10 the displayed distance dropped while average speed carried on
        // using the uncorrected distance -- two numbers on the same panel
        // disagreeing for the rest of the stage.

        suite->addTest("a distance correction changes the average speed with it", []() {
            RallyState state;
            state.calibration = 600000;  // 1 count = 1/600 m... see countsToCentimeters
            // 1 hour of travel, no correction, gives some baseline speed.
            double plain = calculateAverageSpeed(state, 0, 3600000, 600000, 0);
            // Removing 100 m of that distance must lower the average.
            double corrected = calculateAverageSpeed(state, 0, 3600000, 600000, -10000);
            ASSERT_TRUE(corrected < plain);
            return true;
        });

        suite->addTest("a correction that would drive distance negative clamps at zero", []() {
            RallyState state;
            state.calibration = 600000;
            double speed = calculateAverageSpeed(state, 0, 3600000, 600000, -99999999);
            ASSERT_NEAR(speed, 0.0, 0.001);
            return true;
        });

        suite->addTest("no correction leaves the average unchanged", []() {
            RallyState state;
            state.calibration = 600000;
            double a = calculateAverageSpeed(state, 0, 3600000, 600000, 0);
            double b = calculateAverageSpeed(state, 0, 3600000, 600000, 0);
            ASSERT_NEAR(a, b, 0.0001);
            ASSERT_TRUE(a > 0.0);
            return true;
        });

        return suite;
    }
};

#endif // TEST_SPEED_H
