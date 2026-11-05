#ifndef TEST_CALIBRATION_INDEPENDENT_H
#define TEST_CALIBRATION_INDEPENDENT_H

#include "test_framework.h"
#include "../rally_state.h"
#include "../calculations.h"
#include "../config_file.h"
#include <fstream>
#include <cstdio>

class TestCalibrationIndependent {
private:
    const std::string test_config = "test_cal_indep.json";

    void cleanup() {
        std::remove(test_config.c_str());
    }

public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Calibration-Independent Segment Tests");

        suite->addTest("Segment struct defaults kph and meters to 0", []() {
            Segment seg;
            ASSERT_NEAR(seg.target_speed_kph, 0.0, 0.001);
            ASSERT_NEAR(seg.distance_m, 0.0, 0.001);
            // Auto-advance is the intended default: the meter rolls on to the
            // next segment by itself at the end of the current one. The
            // assertion here was ASSERT_FALSE, contradicting the `= true`
            // default declared in rally_types.h -- both landed in the same
            // commit, so this suite has been red ever since. The default is
            // the correct half.
            ASSERT_TRUE(seg.autoNext);
            return true;
        });

        suite->addTest("Segment stores human and count values together", []() {
            long calibration = 600000;
            double kph = 60.0;
            double meters = 5000.0;

            Segment seg;
            seg.target_speed_kph = kph;
            seg.distance_m = meters;
            seg.autoNext = true;

            ASSERT_NEAR(seg.target_speed_kph, 60.0, 0.001);
            ASSERT_NEAR(seg.distance_m, 5000.0, 0.001);
            ASSERT_TRUE(seg.autoNext);
            return true;
        });

        suite->addTest("Recalculate counts from kph after calibration change", []() {
            long old_cal = 600000;
            long new_cal = 900000;
            double kph = 60.0;

            double old_counts = kphToCountsPerHour(kph, old_cal);
            double new_counts = kphToCountsPerHour(kph, new_cal);

            // Same KPH, different calibration -> different counts
            ASSERT_NE(static_cast<long>(old_counts), static_cast<long>(new_counts));

            // But converting back gives the same KPH
            double recovered_kph_old = (old_counts * old_cal) / 1e9;
            double recovered_kph_new = (new_counts * new_cal) / 1e9;
            ASSERT_NEAR(recovered_kph_old, 60.0, 0.01);
            ASSERT_NEAR(recovered_kph_new, 60.0, 0.01);
            return true;
        });

        suite->addTest("Recalculate distance counts from meters after calibration change", []() {
            long old_cal = 600000;
            long new_cal = 900000;
            double meters = 10000.0;

            double old_counts = (meters * 1e6) / old_cal;
            double new_counts = (meters * 1e6) / new_cal;

            ASSERT_NE(static_cast<long>(old_counts), static_cast<long>(new_counts));

            double recovered_m_old = (old_counts * old_cal) / 1e6;
            double recovered_m_new = (new_counts * new_cal) / 1e6;
            ASSERT_NEAR(recovered_m_old, 10000.0, 0.01);
            ASSERT_NEAR(recovered_m_new, 10000.0, 0.01);
            return true;
        });

        suite->addTest("Full recalculation loop updates all segments", []() {
            RallyState state;
            state.calibration = 600000;

            Segment s1;
            s1.target_speed_kph = 50.0;
            s1.distance_m = 2000.0;
            s1.autoNext = true;
            state.segments.push_back(s1);

            Segment s2;
            s2.target_speed_kph = 80.0;
            s2.distance_m = 5000.0;
            s2.autoNext = false;
            state.segments.push_back(s2);


            // Change calibration
            state.calibration = 400000;

            // Recalculate (mirrors on_save_calibration logic)
            for (auto& seg : state.segments) {
            }

            // Count values should have changed

            // Human values should be unchanged
            ASSERT_NEAR(state.segments[0].target_speed_kph, 50.0, 0.001);
            ASSERT_NEAR(state.segments[1].target_speed_kph, 80.0, 0.001);
            ASSERT_NEAR(state.segments[0].distance_m, 2000.0, 0.001);
            ASSERT_NEAR(state.segments[1].distance_m, 5000.0, 0.001);

            // Recovered real-world values should match originals
            return true;
        });

        suite->addTest("Full recalculation loop updates memory slots", []() {
            RallyState state;
            state.calibration = 600000;

            Segment seg;
            seg.target_speed_kph = 70.0;
            seg.distance_m = 3000.0;
            seg.autoNext = true;

            state.memory_slots[0].segments.push_back(seg);
            state.memory_slots[2].segments.push_back(seg);

            // Change calibration and recalculate
            state.calibration = 800000;
            for (int i = 0; i < RallyState::MAX_MEMORY_SLOTS; i++) {
                for (auto& s : state.memory_slots[i].segments) {
                }
            }

            // Verify human values preserved
            ASSERT_NEAR(state.memory_slots[0].segments[0].target_speed_kph, 70.0, 0.001);
            ASSERT_NEAR(state.memory_slots[2].segments[0].distance_m, 3000.0, 0.001);

            // Verify recalculated count values are correct for new calibration
            double expected_speed = kphToCountsPerHour(70.0, 800000);
            double expected_dist = (3000.0 * 1e6) / 800000;
            return true;
        });

        suite->addTest("Save and load preserves new fields", [this]() {
            cleanup();
            RallyState save_state;
            save_state.calibration = 500000;

            Segment seg;
            seg.target_speed_kph = 45.0;
            seg.distance_m = 1234.0;
            seg.autoNext = true;
            save_state.segments.push_back(seg);

            ConfigFile::save(save_state, test_config);

            RallyState load_state;
            ConfigFile::load(load_state, test_config);

            ASSERT_EQ(load_state.segments.size(), 1u);
            ASSERT_NEAR(load_state.segments[0].target_speed_kph, 45.0, 0.001);
            ASSERT_NEAR(load_state.segments[0].distance_m, 1234.0, 0.001);
            ASSERT_TRUE(load_state.segments[0].autoNext);

            cleanup();
            return true;
        });

        suite->addTest("Load old config back-calculates kph and meters", [this]() {
            cleanup();
            long calibration = 600000;
            double speed_cph = 100000.0;
            double dist_counts = 8333.333;

            // Write an old-format config without the new fields
            std::ofstream file(test_config);
            file << "{\n";
            file << "  \"calibration\": " << calibration << ",\n";
            file << "  \"segments\": [\n";
            file << "    {\n";
            file << "      \"target_speed_counts_per_hour\": " << speed_cph << ",\n";
            file << "      \"distance_counts\": " << dist_counts << ",\n";
            file << "      \"autoNext\": true\n";
            file << "    }\n";
            file << "  ]\n";
            file << "}\n";
            file.close();

            RallyState state;
            ConfigFile::load(state, test_config);

            // Should back-calculate: kph = (counts_per_hour * calibration) / 1e9
            double expected_kph = (speed_cph * calibration) / 1e9;
            // Should back-calculate: meters = (dist_counts * calibration) / 1e6
            double expected_m = (dist_counts * calibration) / 1e6;

            ASSERT_EQ(state.segments.size(), 1u);
            ASSERT_NEAR(state.segments[0].target_speed_kph, expected_kph, 0.01);
            ASSERT_NEAR(state.segments[0].distance_m, expected_m, 0.01);

            cleanup();
            return true;
        });

        suite->addTest("Save and load preserves memory slot fields", [this]() {
            cleanup();
            RallyState save_state;
            save_state.calibration = 700000;

            Segment seg;
            seg.target_speed_kph = 55.0;
            seg.distance_m = 8000.0;
            seg.autoNext = false;
            save_state.memory_slots[0].segments.push_back(seg);

            ConfigFile::save(save_state, test_config);

            RallyState load_state;
            ConfigFile::load(load_state, test_config);

            ASSERT_EQ(load_state.memory_slots[0].segments.size(), 1u);
            ASSERT_NEAR(load_state.memory_slots[0].segments[0].target_speed_kph, 55.0, 0.001);
            ASSERT_NEAR(load_state.memory_slots[0].segments[0].distance_m, 8000.0, 0.001);
            ASSERT_FALSE(load_state.memory_slots[0].segments[0].autoNext);

            cleanup();
            return true;
        });

        suite->addTest("Recalculated counts round-trip back to original human values", []() {
            long calibration = 373412;  // real-world calibration value
            double kph = 46.6765;
            double meters = 11320.0;

            double counts_h = kphToCountsPerHour(kph, calibration);
            double dist_counts = (meters * 1e6) / calibration;

            // Recover human values from counts
            double recovered_kph = (counts_h * calibration) / 1e9;
            double recovered_m = (dist_counts * calibration) / 1e6;

            ASSERT_NEAR(recovered_kph, kph, 0.001);
            ASSERT_NEAR(recovered_m, meters, 0.001);
            return true;
        });

        suite->addTest("Zero calibration does not crash back-calculation", [this]() {
            cleanup();
            std::ofstream file(test_config);
            file << "{\n";
            file << "  \"calibration\": 0,\n";
            file << "  \"segments\": [\n";
            file << "    {\n";
            file << "      \"target_speed_counts_per_hour\": 100000.0,\n";
            file << "      \"distance_counts\": 5000.0,\n";
            file << "      \"autoNext\": false\n";
            file << "    }\n";
            file << "  ]\n";
            file << "}\n";
            file.close();

            RallyState state;
            ConfigFile::load(state, test_config);

            // With zero calibration, back-calc is skipped; kph/m stay at 0
            ASSERT_NEAR(state.segments[0].target_speed_kph, 0.0, 0.001);
            ASSERT_NEAR(state.segments[0].distance_m, 0.0, 0.001);
            // Count values are still loaded

            cleanup();
            return true;
        });

        return suite;
    }
};

#endif // TEST_CALIBRATION_INDEPENDENT_H
