#ifndef TEST_STATE_HELPERS_H
#define TEST_STATE_HELPERS_H

#include "test_framework.h"
#include "../calculations.h"
#include "../rally_state.h"
#include "../reaction_offset.h"
#include "../sector.h"
#include "../stage_rows.h"

// The small state operations that used to be written out at every call site: one function each now.
class TestStateHelpers {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("State Helper Tests");

        suite->addTest("startSegmentHere moves the segment and Trip baselines together", []() {
            RallyState s;
            s.total_distance_adjust_mm = -7000;
            s.trip_distance_adjust_mm = -3000;
            s.segment_start_adjust_mm = 123;
            s.segment_current_number = 2;

            startSegmentHere(s, 5000, 5004, 777000);

            ASSERT_EQ(s.segment_start_cntr1, 5000u);
            ASSERT_EQ(s.segment_start_cntr2, 5004u);
            ASSERT_EQ(s.segment_start_adjust_mm, -7000);   // the correction as it stands
            ASSERT_EQ(s.segment_start_time_ms, 777000);
            ASSERT_EQ(s.trip_start_cntr1, 5000u);
            ASSERT_EQ(s.trip_start_cntr2, 5004u);
            ASSERT_EQ(s.trip_start_time_ms, 777000);       // Trip takes the segment's time
            ASSERT_EQ(s.trip_distance_adjust_mm, 0);       // and spends its correction
            ASSERT_EQ(s.total_distance_adjust_mm, -7000);  // Total's correction stands
            ASSERT_EQ(s.segment_current_number, 2);        // the caller steps the row
            return true;
        });

        suite->addTest("startSegmentHere is rebaseSegmentAt at zero plus the time and Trip", []() {
            RallyState a, b;
            a.total_distance_adjust_mm = b.total_distance_adjust_mm = 4000;
            a.calibration = b.calibration = 0;   // the unusable-calibration branch too
            startSegmentHere(a, 100, 200, 55);
            rebaseSegmentAt(b, 100, 200, 0);
            b.segment_start_time_ms = 55;
            rebaseTripToSegment(b, 100, 200);
            ASSERT_EQ(a.segment_start_cntr1, b.segment_start_cntr1);
            ASSERT_EQ(a.segment_start_cntr2, b.segment_start_cntr2);
            ASSERT_EQ(a.segment_start_adjust_mm, b.segment_start_adjust_mm);
            ASSERT_EQ(a.trip_start_time_ms, b.trip_start_time_ms);
            return true;
        });

        suite->addTest("navi set makes navi read the figure; a negative one changes nothing", []() {
            RallyState s;
            s.calibration = 1000000;
            ASSERT_TRUE(applyNaviSetOrReset(s, 2000, 2000, false, 1500.0));
            ASSERT_EQ(odometerMm(s, 2000, 2000), 1500000);
            const long before = s.odometer_adjust_mm;
            ASSERT_FALSE(applyNaviSetOrReset(s, 2000, 2000, false, -1.0));
            ASSERT_EQ(s.odometer_adjust_mm, before);
            return true;
        });

        suite->addTest("navi reset zeroes navi and keeps the roadbook position", []() {
            RallyState s;
            s.calibration = 1000000;
            s.odometer_adjust_mm = 250000;
            s.sector_start_odometer_mm = 100000;
            const long position_mm = static_cast<long>(odometerMm(s, 3000, 3000)) - s.sector_start_odometer_mm;
            ASSERT_TRUE(applyNaviSetOrReset(s, 3000, 3000, true, 99999.0));   // metres unused on a reset
            ASSERT_EQ(odometerMm(s, 3000, 3000), 0);
            ASSERT_EQ(s.odometer_start_cntr1, 3000u);
            ASSERT_EQ(static_cast<long>(odometerMm(s, 3000, 3000)) - s.sector_start_odometer_mm, position_mm);
            return true;
        });

        suite->addTest("phone lead clamps to 0..400 ms", []() {
            ASSERT_EQ(clampPhoneLeadMs(-10), 0);
            ASSERT_EQ(clampPhoneLeadMs(0), 0);
            ASSERT_EQ(clampPhoneLeadMs(110), 110);
            ASSERT_EQ(clampPhoneLeadMs(400), 400);
            ASSERT_EQ(clampPhoneLeadMs(410), 400);
            return true;
        });

        suite->addTest("tone type is 1 or 2", []() {
            ASSERT_TRUE(validToneType(1));
            ASSERT_TRUE(validToneType(2));
            ASSERT_FALSE(validToneType(0));
            ASSERT_FALSE(validToneType(3));
            ASSERT_FALSE(validToneType(-1));
            return true;
        });

        suite->addTest("a segment row must be inside the roadbook on show", []() {
            RallyState s;
            s.segments.resize(3);
            ASSERT_FALSE(validSegmentRow(s, -1));
            ASSERT_TRUE(validSegmentRow(s, 0));
            ASSERT_TRUE(validSegmentRow(s, 2));
            ASSERT_FALSE(validSegmentRow(s, 3));
            RallyState empty;
            ASSERT_FALSE(validSegmentRow(empty, 0));
            return true;
        });

        suite->addTest("a measured calibration in metres sets calibration and clears typed pulses", []() {
            RallyState s;
            s.counters = false;
            s.distance_unit = DIST_KM;
            s.typed_pulses_per_km = 1234.0;
            ASSERT_TRUE(applyMeasuredCalibration(s, "1000", 1000, 1000, 0, 0));   // 1000 counts over 1000 m
            ASSERT_EQ(s.calibration, 1000000);
            ASSERT_EQ(s.typed_pulses_per_km, 0.0);
            return true;
        });

        suite->addTest("calibration sensor line: no colons after 1, 2 and Avg (RB-CAL-12)", []() {
            ASSERT_EQ(sensorCountMarkup(3236, 3238, 3237),
                      std::string("Sensor Count: 1 <span foreground=\"#FFFFFF\">3236</span>"
                                  " 2 <span foreground=\"#FFFFFF\">3238</span>"
                                  " Avg <span foreground=\"#FFFFFF\">3237</span>"));
            ASSERT_EQ(sensorCountMarkup(-5, 0, -2).find("1 <span foreground=\"#FFFFFF\">-5</span>") != std::string::npos, true);
            return true;
        });

        suite->addTest("a measured calibration in miles keeps the millimetres", []() {
            RallyState s;
            s.counters = false;
            s.distance_unit = DIST_MI;
            ASSERT_TRUE(applyMeasuredCalibration(s, "1", 2000, 2000, 0, 0));   // 2000 counts over one mile
            ASSERT_EQ(s.calibration, 1609344 * 1000 / 2000);
            return true;
        });

        suite->addTest("a measured calibration refuses what the box refuses", []() {
            RallyState s;
            s.counters = false;
            s.distance_unit = DIST_KM;
            s.calibration = 555555;
            ASSERT_FALSE(applyMeasuredCalibration(s, "499", 1000, 1000, 0, 0));      // under 500 m
            ASSERT_FALSE(applyMeasuredCalibration(s, "100001", 1000, 1000, 0, 0));   // over 100 km
            ASSERT_FALSE(applyMeasuredCalibration(s, "1000", 500, 500, 500, 500));   // nothing driven
            ASSERT_FALSE(applyMeasuredCalibration(s, "", 1000, 1000, 0, 0));
            ASSERT_FALSE(applyMeasuredCalibration(s, nullptr, 1000, 1000, 0, 0));
            s.distance_unit = DIST_MI;
            ASSERT_FALSE(applyMeasuredCalibration(s, "abc", 1000, 1000, 0, 0));      // a miles typo is a no-op
            ASSERT_EQ(s.calibration, 555555);
            return true;
        });

        suite->addTest("a measured calibration refuses text that is not a plain number, in metres, km and miles", []() {
            const char* bad[] = {"abc", "", "12abc", "12.5x", "1000x", "1000.5.5", "1000:30", "x1000", "99999999999999999999999", "1e3"};
            for (int unit : {DIST_M, DIST_KM, DIST_MI}) {
                for (const char* text : bad) {
                    RallyState s;
                    s.counters = false;
                    s.distance_unit = unit;
                    s.calibration = 555555;
                    s.typed_pulses_per_km = 77.0;
                    ASSERT_FALSE(applyMeasuredCalibration(s, text, 1000, 1000, 0, 0));
                    ASSERT_EQ(s.calibration, 555555);
                    ASSERT_EQ(s.typed_pulses_per_km, 77.0);
                }
            }
            return true;
        });

        suite->addTest("a measured calibration in metres still reads what it always read", []() {
            for (int unit : {DIST_M, DIST_KM}) {
                for (const char* text : {"1000", " 1000", "1000 ", "+1000", "1000.5", "1000.", "1000.999"}) {
                    RallyState s;
                    s.counters = false;
                    s.distance_unit = unit;
                    ASSERT_TRUE(applyMeasuredCalibration(s, text, 1000, 1000, 0, 0));
                    ASSERT_EQ(s.calibration, 1000000);   // a fractional part is dropped, as before
                }
            }
            return true;
        });

        suite->addTest("a pulses-per-km calibration is shown back as typed; zero changes nothing", []() {
            RallyState s;
            s.calibration = 777777;
            ASSERT_TRUE(applyPulsesPerKmCalibration(s, 1000.0));
            ASSERT_EQ(s.calibration, 1000000);
            ASSERT_EQ(s.typed_pulses_per_km, 1000.0);
            ASSERT_FALSE(applyPulsesPerKmCalibration(s, 0.0));
            ASSERT_FALSE(applyPulsesPerKmCalibration(s, -5.0));
            ASSERT_EQ(s.calibration, 1000000);
            return true;
        });

        return suite;
    }
};

#endif // TEST_STATE_HELPERS_H
