#ifndef TEST_SETTINGS_H
#define TEST_SETTINGS_H

#include "test_framework.h"
#include "../tone_cadence.h"
#include "../config_file.h"
#include "../rally_state.h"
#include <cstdio>
#include <fstream>

// RB-SET-01: the Settings page's data -- tone type per side of schedule, the
// logging switches, and files written before they existed.
class TestSettings {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Settings (RB-SET-01)");

        suite->addTest("behind schedule uses the behind type, on time and ahead the ahead type", []() {
            return  simpleToneForError(-1.0, 2, 1) && !simpleToneForError(-1.0, 1, 2)
                && !simpleToneForError( 1.0, 2, 1) &&  simpleToneForError( 1.0, 1, 2)
                && !simpleToneForError( 0.0, 2, 1) &&  simpleToneForError( 0.0, 1, 2);
        });

        suite->addTest("tone types, logging switches and beep modes survive a round trip", []() {
            const char* path = "test_settings_cfg.json";
            RallyState a;
            a.tone_type_behind = 2; a.tone_type_ahead = 1;
            a.stage_logging = false; a.debug_logs_enabled = true;
            a.beep_navigation_mode = true; a.beep_timing_mode = false;
            ConfigFile::save(a, path);
            RallyState b;
            b.stage_logging = true; b.debug_logs_enabled = false;
            ConfigFile::load(b, path);
            std::remove(path);
            return b.tone_type_behind == 2 && b.tone_type_ahead == 1
                && !b.stage_logging && b.debug_logs_enabled
                && b.beep_navigation_mode && !b.beep_timing_mode;
        });

        suite->addTest("an older file's single tone type applies to both sides", []() {
            const char* path = "test_settings_old.json";
            { std::ofstream f(path); f << "{\n  \"simple_tone_mode\": true,\n  \"tone_enabled\": true\n}\n"; }
            RallyState s;
            ConfigFile::load(s, path);
            std::remove(path);
            return s.tone_type_behind == 2 && s.tone_type_ahead == 2;
        });

        suite->addTest("defaults: stage logging on, sensor logging off, type 1 both sides", []() {
            RallyState s;
            return s.stage_logging && !s.debug_logs_enabled
                && s.tone_type_behind == 1 && s.tone_type_ahead == 1;
        });

        return suite;
    }
};

#endif // TEST_SETTINGS_H
