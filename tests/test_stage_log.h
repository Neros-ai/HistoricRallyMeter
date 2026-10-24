#ifndef TEST_STAGE_LOG_H
#define TEST_STAGE_LOG_H

#include "test_framework.h"
#include "../stage_log.h"
#include "../debug_log.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <utime.h>

// RB-DEV-15: the once-a-second stage CSV and the 60-day purge it shares with
// the sensor log.
class TestStageLog {
    static std::string freshDir(const char* name) {
        std::string d = std::string("/tmp/rb_stage_log_test_") + name;
        std::string cmd = "rm -rf " + d;
        if (system(cmd.c_str()) != 0) {}
        return d;
    }
    static std::vector<std::string> lines(const std::string& path) {
        std::vector<std::string> out;
        std::ifstream in(path);
        std::string l;
        while (std::getline(in, l)) out.push_back(l);
        return out;
    }
    static void touch(const std::string& path, int64_t mtime_s) {
        std::ofstream(path) << "x\n";
        struct utimbuf t{ static_cast<time_t>(mtime_s), static_cast<time_t>(mtime_s) };
        utime(path.c_str(), &t);
    }
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Stage Log (RB-DEV-15)");

        suite->addTest("start writes the header into stage<N>_*.csv", []() {
            std::string dir = freshDir("hdr");
            StageLog log;
            if (!log.start(dir, 1760000000000LL, 3)) return false;
            auto l = lines(log.path());
            bool ok = !l.empty() && l[0] == "time,time_ms,distance_m,speed_kph,ahead_behind_s"
                   && log.path().find("/stage3_") != std::string::npos;
            log.stop();
            return ok;
        });

        suite->addTest("rows buffer ten at a time and stop writes the rest", []() {
            std::string dir = freshDir("buf");
            StageLog log;
            log.start(dir, 1760000000000LL, 1);
            std::string path = log.path();
            for (int i = 0; i < 9; i++) log.record(1760000001000LL + i * 1000, i, 40.0, 0.0);
            bool nine = lines(path).size() == 1 && log.buffered() == 9;   // header only
            log.record(1760000010000LL, 9, 40.0, 0.0);
            bool ten = lines(path).size() == 11 && log.buffered() == 0;
            log.record(1760000011000LL, 10, 40.0, 0.0);
            log.stop();
            return nine && ten && lines(path).size() == 12;
        });

        suite->addTest("row format: 1 dp speed, signed 1 dp ahead/behind", []() {
            std::string row = StageLog::formatRow(1760000000123LL, 1820, 47.46, -1.04);
            const std::string tail = ",1820,47.5,-1.0";
            return row.size() > tail.size() && row.substr(row.size() - tail.size()) == tail
                && StageLog::formatRow(1760000000123LL, 0, 0.0, 0.04).find(",+0.0") != std::string::npos;
        });

        suite->addTest("two starts in the same second get different files", []() {
            std::string dir = freshDir("same");
            StageLog a, b;
            a.start(dir, 1760000000000LL, 2);
            b.start(dir, 1760000000000LL, 2);
            bool ok = a.path() != b.path() && !b.path().empty();
            a.stop(); b.stop();
            return ok;
        });

        suite->addTest("an unwritable directory stops logging without crashing", []() {
            StageLog log;
            bool started = log.start("/nonexistent_rb_root/x", 1760000000000LL, 1);
            log.record(1760000001000LL, 1, 1.0, 1.0);
            return !started && !log.isRunning() && log.buffered() == 0;
        });

        suite->addTest("the file name passes the phone's download check", []() {
            std::string dir = freshDir("name");
            StageLog log;
            log.start(dir, 1760000000000LL, 4);
            std::string p = log.path();
            std::string name = p.substr(p.rfind('/') + 1);
            log.stop();
            return DebugLog::isSafeFileName(name);
        });

        suite->addTest("60-day purge takes old stage and sensor logs, keeps newer and non-csv", []() {
            std::string dir = freshDir("purge");
            mkdir(dir.c_str(), 0755);
            const int64_t now_s = 1760000000;
            const int64_t day = 86400;
            touch(dir + "/stage1_old.csv", now_s - 61 * day);
            touch(dir + "/2026-01-01_00-00-00.csv", now_s - 61 * day);
            touch(dir + "/stage1_new.csv", now_s - 59 * day);
            touch(dir + "/2026-02-01_00-00-00.csv", now_s - 59 * day);
            touch(dir + "/notes.txt", now_s - 400 * day);
            int deleted = DebugLog::purgeOlderThan(dir, now_s * 1000, 60);
            struct stat st;
            return deleted == 2
                && stat((dir + "/stage1_old.csv").c_str(), &st) != 0
                && stat((dir + "/stage1_new.csv").c_str(), &st) == 0
                && stat((dir + "/notes.txt").c_str(), &st) == 0;
        });

        suite->addTest("log kind by prefix", []() {
            return logKind("stage3_2026-10-12_21-31-00.csv") == "stage"
                && logKind("2026-10-12_21-31-00.csv") == "sensor";
        });

        return suite;
    }
};

#endif // TEST_STAGE_LOG_H
