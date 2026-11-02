#ifndef TEST_SURREY_H
#define TEST_SURREY_H

#include "test_framework.h"
#include "../surrey.h"
#include "../config_file.h"
#include "../rally_state.h"
#include <cstdio>
#include <fstream>

// RB-NAV-43: surrey is the third live rally type (0 greek, 1 surrey, 2 italian).
class TestSurrey {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Surrey Tests");

        suite->addTest("rally type surrey round-trips", []() {
            const char* path = "test_surrey_type.json";
            RallyState a; a.rally_type = 1;
            ConfigFile::save(a, path);
            RallyState b;
            ConfigFile::load(b, path);
            std::remove(path);
            return b.rally_type == 1 && isSurrey(b) && !isSurrey(RallyState());
        });

        suite->addTest("surrey defaults: metres, AB,AC", []() {
            RallyState s;
            applyRallyTypeDefaults(s, 2);
            s.measure_ab_bc = true;
            applyRallyTypeDefaults(s, 1);
            return s.rally_type == 1 && !s.distance_km && !s.measure_ab_bc;
        });

        suite->addTest("time taken round-trips; an old config gives -1", []() {
            const char* path = "test_surrey_taken.json";
            RallyState a; a.time_taken_ms = 560600; a.surrey_fit_failed = true;
            ConfigFile::save(a, path);
            RallyState b;
            ConfigFile::load(b, path);
            const bool ok = b.time_taken_ms == 560600 && b.surrey_fit_failed;
            { std::ofstream f(path); f << "{\n  \"rally_type\": 1\n}\n"; }
            RallyState c;
            ConfigFile::load(c, path);
            std::remove(path);
            return ok && c.time_taken_ms == -1 && !c.surrey_fit_failed && c.surrey_finish_at_ms == 0;
        });

        return suite;
    }
};

#endif
