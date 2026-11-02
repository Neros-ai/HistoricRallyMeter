#ifndef TEST_STAGE_STORE_H
#define TEST_STAGE_STORE_H

#include "test_framework.h"
#include "../stage_store.h"
#include "../config_file.h"
#include "../rally_state.h"
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

        suite->addTest("plus skips gaps", []() {
            RallyState s; program(s, 16); program(s, 18); program(s, 21);
            return nextProgrammedStage(s, 18, +1) == 21 && nextProgrammedStage(s, 16, +1) == 18;
        });

        suite->addTest("minus skips gaps", []() {
            RallyState s; program(s, 16); program(s, 18); program(s, 21);
            return nextProgrammedStage(s, 21, -1) == 18 && nextProgrammedStage(s, 18, -1) == 16;
        });

        suite->addTest("minus at the lowest and plus at the highest stay put", []() {
            RallyState s; program(s, 16); program(s, 21);
            return nextProgrammedStage(s, 16, -1) == 16 && nextProgrammedStage(s, 21, +1) == 21;
        });

        suite->addTest("from an empty number the nearest programmed stage in that direction", []() {
            RallyState s; program(s, 16); program(s, 21);
            return nextProgrammedStage(s, 18, +1) == 21 && nextProgrammedStage(s, 18, -1) == 16;
        });

        suite->addTest("no programmed stage at all stays put", []() {
            RallyState s;
            return nextProgrammedStage(s, 5, +1) == 5 && programmedStages(s).empty();
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
