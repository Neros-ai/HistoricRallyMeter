#ifndef TEST_STAGE_STORE_H
#define TEST_STAGE_STORE_H

#include "test_framework.h"
#include "../stage_store.h"
#include "../config_file.h"
#include "../rally_state.h"
#include <cmath>
#include <cstdio>

// RB-NAV-45: stages numbered 1-99 (the organiser's own numbers).
class TestStageStore {
public:
    static void program(RallyState& s, int stage) {
        Segment sg; sg.distance_m = 1000.0; sg.target_speed_kph = 40.0;
        s.memory_slots[stage - 1].segments.push_back(sg);
    }

    TestSuite* createSuite() {
        auto* suite = new TestSuite("Stage numbers (RB-NAV-45)");

        suite->addTest("no programmed stage at all lists none", []() {
            RallyState s;
            return programmedStages(s).empty();
        });

        suite->addTest("km to miles reinterprets every programmed stage and the live rows; format changes do not", []() {
            RallyState s;
            s.distance_unit = DIST_KM3;
            for (int stage : {1, 7}) {
                Segment sg; sg.distance_m = 5000.0; sg.target_speed_kph = 40.0;
                s.memory_slots[stage - 1].segments.push_back(sg);
                s.memory_slots[stage - 1].start_distance_m = 2000;
            }
            s.last_memory_slot = 1;
            s.segments = s.memory_slots[0].segments;
            s.segment_current_number = -1;
            s.stage_complete = true;   // nothing live
            if (!changeDistanceUnit(s, DIST_KM)) return false;                       // format only
            if (s.distance_unit != DIST_KM || s.memory_slots[0].segments[0].distance_m != 5000.0) return false;
            if (!changeDistanceUnit(s, DIST_MI)) return false;                       // km -> miles
            const double five_mi = 5.0 * 1609.344;
            return s.distance_unit == DIST_MI
                && std::fabs(s.memory_slots[0].segments[0].distance_m - five_mi) < 1e-9
                && std::fabs(s.memory_slots[6].segments[0].distance_m - five_mi) < 1e-9
                && std::fabs(s.segments[0].distance_m - five_mi) < 1e-9
                && s.memory_slots[6].start_distance_m == std::lround(2.0 * 1609.344);
        });

        suite->addTest("km to miles is refused while a stage is live", []() {
            RallyState s;
            s.distance_unit = DIST_KM3;
            Segment sg; sg.distance_m = 5000.0;
            s.memory_slots[0].segments.push_back(sg);
            s.segments = s.memory_slots[0].segments;
            s.last_memory_slot = 1;
            s.stage_complete = false;
            s.segment_current_number = 1;   // running
            return !changeDistanceUnit(s, DIST_MI) && s.distance_unit == DIST_KM3
                && s.memory_slots[0].segments[0].distance_m == 5000.0;
        });

        suite->addTest("italian: km to miles re-works a pc's speed from its new distance", []() {
            RallyState s;
            s.rally_type = RALLY_ITALIAN;
            s.distance_unit = DIST_KM;
            Segment pc; pc.type = SEG_PC; pc.ref = 1; pc.distance_m = 560.0; pc.pc_duration_s = 31.0;
            pc.target_speed_kph = 560.0 / 31.0 * 3.6;
            s.memory_slots[0].segments.push_back(pc);
            s.memory_slots[3].segments.push_back(pc);
            s.last_memory_slot = 1;
            s.segments = s.memory_slots[0].segments;
            s.segment_current_number = -1;
            s.stage_complete = true;
            if (!changeDistanceUnit(s, DIST_MI)) return false;
            const double want = 0.56 * 1609.344 / 31.0 * 3.6;   // 0.56 mile in 31 s
            return std::fabs(s.segments[0].target_speed_kph - want) < 1e-6
                && std::fabs(s.memory_slots[0].segments[0].target_speed_kph - want) < 1e-6
                && std::fabs(s.memory_slots[3].segments[0].target_speed_kph - want) < 1e-6;
        });

        suite->addTest("valid stage numbers are 1 to 99", []() {
            return !validStageNumber(0) && validStageNumber(1) && validStageNumber(99) && !validStageNumber(100)
                && !validStageNumber(-3);
        });

        suite->addTest("old five memories become stages 1 to 5", []() {
            const char* path = "test_stage_store_old.json";
            RallyState a;
            for (int i = 1; i <= 5; i++) program(a, i);
            ConfigFile::save(a, path);
            RallyState b;
            ConfigFile::load(b, path);
            std::remove(path);
            std::vector<int> want = {1, 2, 3, 4, 5};
            return programmedStages(b) == want;
        });

        suite->addTest("stage 99 and stage 42 round-trip with their start time and distance", []() {
            const char* path = "test_stage_store_99.json";
            RallyState a;
            program(a, 99); program(a, 42);
            a.memory_slots[41].start_time_s = 37800;
            a.memory_slots[41].start_distance_m = 1250;
            a.last_memory_slot = 99;
            ConfigFile::save(a, path);
            RallyState b;
            ConfigFile::load(b, path);
            std::remove(path);
            std::vector<int> want = {42, 99};
            return programmedStages(b) == want && b.last_memory_slot == 99
                && b.memory_slots[41].start_time_s == 37800
                && b.memory_slots[41].start_distance_m == 1250
                && b.memory_slots[98].segments.size() == 1;
        });

        suite->addTest("choose stage: one or two figures are a stage", []() {
            return chosenStageNumber("7") == 7 && chosenStageNumber("07") == 7
                && chosenStageNumber("42") == 42 && chosenStageNumber("99") == 99;
        });

        suite->addTest("choose stage: empty, 0, 100 or not a number is no stage", []() {
            return chosenStageNumber("") == 0 && chosenStageNumber("0") == 0
                && chosenStageNumber("100") == 0 && chosenStageNumber("1a") == 0
                && chosenStageNumber("-1") == 0;
        });

        return suite;
    }
};

#endif
