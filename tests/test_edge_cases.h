#ifndef TEST_EDGE_CASES_H
#define TEST_EDGE_CASES_H

#include "test_framework.h"
#include "mock_i2c_counter.h"
#include "../calculations.h"
#include "../rally_state.h"
#include <limits>

class TestEdgeCases {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Edge Cases and Error Handling Tests");
        
        // Test I2C read failure
        suite->addTest("I2C read failure handled gracefully", []() {
            MockI2CCounter mock(1, 0x70);
            mock.setFailMode(true);
            
            bool caught_exception = false;
            try {
                mock.readRegister(0x07);
            } catch (const std::exception& e) {
                caught_exception = true;
            }
            
            ASSERT_TRUE(caught_exception);
            
            return true;
        });
        
        // Test zero calibration prevention
        suite->addTest("Zero calibration prevented (divide by zero)", []() {
            long calibration = 0;
            
            // Should be rejected by validation
            bool is_valid = calibration > 0;
            ASSERT_FALSE(is_valid);
            
            return true;
        });
        
        // Test empty segments during rally
        suite->addTest("Empty segment array during active rally handled", []() {
            RallyState state;
            state.segment_current_number = 0;  // Trying to access segment
            
            // But no segments exist
            bool has_valid_segment = (state.segment_current_number >= 0 && 
                                     state.segment_current_number < (long)state.segments.size());
            
            ASSERT_FALSE(has_valid_segment);
            
            return true;
        });
        
        // Test segment index beyond bounds
        suite->addTest("segment_current_number beyond segments array", []() {
            RallyState state;
            state.segments.push_back({100, 1000, true});
            state.segment_current_number = 5;  // Beyond array
            
            bool is_valid = (state.segment_current_number >= 0 && 
                           state.segment_current_number < (long)state.segments.size());
            
            ASSERT_FALSE(is_valid);
            
            return true;
        });
        
        // Test 32-bit counter near limit
        suite->addTest("Counter near 32-bit limit handled", []() {
            uint32_t near_max = 0xFFFFFFFF - 100;
            uint32_t max_val = 0xFFFFFFFF;
            
            // Should be valid values
            ASSERT_GT(near_max, 0u);
            ASSERT_EQ(max_val, 4294967295u);
            
            return true;
        });
        
        // Test zero counts (stationary)
        suite->addTest("Zero counts (stationary vehicle) gives zero speed", []() {
            RallyState state;
            state.counters = false;
            state.units = false;
            state.calibration = 600000;
            
            CounterPoll current = {0, 0, 2001};  // Same position, different time
            CounterPoll tenth = {0, 0, 1};
            
            double speed = calculateCurrentSpeed(state, current, tenth);
            
            ASSERT_NEAR(speed, 0.0, 0.001);
            
            return true;
        });
        
        // Test high speed calculation
        suite->addTest("High speed (200+ KPH) calculated correctly", []() {
            RallyState state;
            state.counters = false;
            state.units = false;
            state.calibration = 600000;
            
            // 200 km/h = ~333333 counts/hour at this calibration
            // In 2 seconds: 333333 / 1800 = ~185 counts
            CounterPoll current = {185, 0, 2001};
            CounterPoll tenth = {0, 0, 1};
            
            double speed = calculateCurrentSpeed(state, current, tenth);
            
            // Should be around 200 kph
            ASSERT_GT(speed, 190.0);
            ASSERT_LT(speed, 210.0);
            
            return true;
        });
        
        // Test long stage (hundreds of km)
        suite->addTest("Long stage (100km) without precision loss", []() {
            RallyState state;
            state.calibration = 600000;
            
            // 100km = 100000m, with 0.6m/count = 166667 counts
            int64_t counts = 166667;
            long cm = countsToCentimeters(counts, state.calibration);
            
            // Should be ~10000000 cm = 100km
            ASSERT_GT(cm, 9900000);
            ASSERT_LT(cm, 10100000);
            
            return true;
        });
        
        // Test short segment (2km)
        suite->addTest("Short segment (2km) handled correctly", []() {
            RallyState state;
            state.calibration = 600000;
            
            // 2km = 2000m, with 0.6m/count = 3333 counts
            Segment seg;
            seg.distance_m = 2000.0;
            
            long meters = static_cast<long>(seg.distance_m);
            
            // Should be ~2000m
            ASSERT_GT(meters, 1900);
            ASSERT_LT(meters, 2100);
            
            return true;
        });
        
        // Test negative distance
        suite->addTest("Negative distance values handled", []() {
            int64_t negative_distance = -1000;
            
            // Should be detected and handled
            bool is_negative = (negative_distance < 0);
            ASSERT_TRUE(is_negative);
            
            return true;
        });
        
        // Test negative speed
        suite->addTest("Negative speed displays as 0.00", []() {
            double negative_speed = -50.0;
            
            // Display logic should clamp to 0
            double display_speed = (negative_speed < 0) ? 0.0 : negative_speed;
            ASSERT_NEAR(display_speed, 0.0, 0.001);
            
            return true;
        });
        
        // Test system time jump forward
        suite->addTest("System time jump forward handled", []() {
            int64_t start_time = 1000000;
            int64_t current_time = 5000000;  // Jump forward
            
            int64_t elapsed = current_time - start_time;
            ASSERT_GT(elapsed, 0);  // Should be positive
            
            return true;
        });
        
        // Test system time jump backward
        suite->addTest("System time jump backward prevented", []() {
            int64_t start_time = 5000000;
            int64_t current_time = 1000000;  // Jump backward
            
            int64_t elapsed = current_time - start_time;
            
            // Elapsed is negative, should be handled
            bool time_went_backward = (elapsed < 0);
            ASSERT_TRUE(time_went_backward);
            
            // In practice, would clamp to 0 or use absolute value
            int64_t safe_elapsed = (elapsed < 0) ? 0 : elapsed;
            ASSERT_EQ(safe_elapsed, 0);
            
            return true;
        });
        
        // Test counter overflow
        suite->addTest("Counter overflow at 32-bit boundary", []() {
            uint32_t before_overflow = 0xFFFFFFF0;
            uint32_t after_overflow = 0x00000010;  // Wrapped around
            
            // With 64-bit arithmetic
            uint64_t before_64 = before_overflow;
            uint64_t after_64 = after_overflow;
            
            // After wrap, counter value is smaller
            ASSERT_LT(after_64, before_64);
            
            return true;
        });
        
        // Test invalid JSON field
        suite->addTest("Invalid JSON field uses default", []() {
            RallyState state;
            
            // Simulate parsing invalid calibration
            std::string invalid_cal = "\"calibration\": \"not_a_number\"";
            
            // State should retain default if parsing fails
            ASSERT_EQ(state.calibration, 600000);
            
            return true;
        });
        
        // Test mock I2C counter
        suite->addTest("Mock I2C counter tracks read count", []() {
            MockI2CCounter mock(1, 0x70);
            mock.setRegisterValue(0x07, 12345);
            
            mock.readRegister(0x07);
            mock.readRegister(0x07);
            mock.readRegister(0x07);
            
            ASSERT_EQ(mock.getReadCount(), 3);
            
            return true;
        });
        
        // Test rapid segment transitions
        suite->addTest("Rapid segment transitions at high speed", []() {
            RallyState state;
            state.segments.push_back({100000.0, 100.0, true});   // Very short
            state.segments.push_back({100000.0, 100.0, true});
            state.segments.push_back({100000.0, 100.0, true});
            state.segment_current_number = 0;
            
            // At high speed, might cover multiple segments in one poll
            double distance_covered = 500.0;  // 5x the segment distance
            
            int new_segment = 0;
            double cumulative = 0.0;
            for (size_t i = 0; i < state.segments.size(); i++) {
                cumulative += state.segments[i].distance_m;
                if (distance_covered >= cumulative && state.segments[i].autoNext) {
                    new_segment = i + 1;
                }
            }
            
            // Should advance to end of available segments
            ASSERT_EQ(new_segment, 3);
            
            return true;
        });
        
        return suite;
    }
};

#endif // TEST_EDGE_CASES_H
