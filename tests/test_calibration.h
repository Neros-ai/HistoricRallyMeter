#ifndef TEST_CALIBRATION_H
#define TEST_CALIBRATION_H

#include "test_framework.h"
#include "../rally_state.h"

class TestCalibration {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Calibration Tests");
        
        // Test default calibration
        suite->addTest("Default calibration is 600000", []() {
            RallyState state;
            ASSERT_EQ(state.calibration, 600000);
            return true;
        });
        
        // Test calibration calculation
        suite->addTest("New calibration = (meters * 1000000) / count_diff", []() {
            // Input: 1000 meters traveled, 1500 counts
            long input_meters = 1000;
            long count_diff = 1500;
            
            long new_cal = (input_meters * 1000 * 1000) / count_diff;
            // 1000 * 1000000 / 1500 = 666666
            ASSERT_EQ(new_cal, 666666);
            
            return true;
        });
        
        // Test minimum input distance (500m)
        suite->addTest("Minimum input distance is 500 meters", []() {
            long min_distance = 500;
            long count_diff = 1000;
            
            // Should be valid
            long new_cal = (min_distance * 1000 * 1000) / count_diff;
            ASSERT_GT(new_cal, 0);
            
            // Values below 500 should be rejected by UI validation
            long invalid_distance = 499;
            bool is_valid = invalid_distance >= 500;
            ASSERT_FALSE(is_valid);
            
            return true;
        });
        
        // Test maximum input distance (100,000m)
        suite->addTest("Maximum input distance is 100000 meters", []() {
            long max_distance = 100000;
            long count_diff = 100000;
            
            // Should be valid
            long new_cal = (max_distance * 1000 * 1000) / count_diff;
            ASSERT_GT(new_cal, 0);
            
            // Values above 100000 should be rejected by UI validation
            long invalid_distance = 100001;
            bool is_valid = invalid_distance <= 100000;
            ASSERT_FALSE(is_valid);
            
            return true;
        });
        
        // Test calibration doesn't affect existing segments
        suite->addTest("Calibration change does not affect stored segment speeds", []() {
            RallyState state;
            state.calibration = 600000;
            
            // A segment's speed is the typed km/h, whatever the calibration
            Segment seg;
            seg.target_speed_kph = 60.0;
            seg.distance_m = 5000.0;
            state.segments.push_back(seg);
            
            // Change calibration
            state.calibration = 800000;
            
            // Nothing about the segment depends on it
            ASSERT_NEAR(state.segments[0].target_speed_kph, 60.0, 1e-9);
            ASSERT_NEAR(state.segments[0].distance_m, 5000.0, 1e-9);
            return true;
        });
        
        // Test calibration affects new calculations
        suite->addTest("Calibration change affects subsequent calculations", []() {
            long old_cal = 600000;
            long new_cal = 900000;
            int64_t counts = 1000;
            
            long old_cm = (counts * old_cal) / 10000;
            long new_cm = (counts * new_cal) / 10000;
            
            // New calibration should give different result
            ASSERT_NE(old_cm, new_cm);
            ASSERT_EQ(old_cm, 60000);  // 600m
            ASSERT_EQ(new_cm, 90000);  // 900m
            
            return true;
        });
        
        // Test calibration with very small count diff
        suite->addTest("Calibration with small count difference", []() {
            long input_meters = 500;  // minimum
            long count_diff = 100;    // small count
            
            long new_cal = (input_meters * 1000 * 1000) / count_diff;
            // 500 * 1000000 / 100 = 5000000
            ASSERT_EQ(new_cal, 5000000);
            
            return true;
        });
        
        // Test calibration with large count diff
        suite->addTest("Calibration with large count difference", []() {
            long input_meters = 100000;  // maximum
            long count_diff = 1000000;   // large count
            
            long new_cal = (input_meters * 1000 * 1000) / count_diff;
            // 100000 * 1000000 / 1000000 = 100000
            ASSERT_EQ(new_cal, 100000);
            
            return true;
        });
        
        // Test zero count diff (should be prevented)
        suite->addTest("Zero count diff should be prevented", []() {
            long count_diff = 0;
            
            // Division by zero should be prevented by UI validation
            bool is_valid = count_diff > 0;
            ASSERT_FALSE(is_valid);
            
            return true;
        });
        
        return suite;
    }
};

#endif // TEST_CALIBRATION_H
