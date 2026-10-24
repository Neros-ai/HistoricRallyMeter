#ifndef TEST_DEBUG_LOG_H
#define TEST_DEBUG_LOG_H

#include "test_framework.h"
#include "../debug_log.h"
#include <fstream>
#include <string>
#include <vector>
#include <cstdio>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// Debug log: CSV of every counter poll, buffered 100 rows at a time.
class TestDebugLog {
private:
    const std::string dir = "test_debug_logs";

    std::vector<std::string> readLines(const std::string& path) {
        std::vector<std::string> lines;
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) lines.push_back(line);
        return lines;
    }

    void cleanup(const std::string& path) {
        if (!path.empty()) std::remove(path.c_str());
        rmdir(dir.c_str());
    }

    // Set a file's modified time (seconds since the epoch).
    void touch(const std::string& path, int64_t mtime_s) {
        struct timespec ts[2];
        ts[0].tv_sec = mtime_s; ts[0].tv_nsec = 0;
        ts[1].tv_sec = mtime_s; ts[1].tv_nsec = 0;
        utimensat(AT_FDCWD, path.c_str(), ts, 0);
    }

    // 2026-10-07 12:34:56.789 local time, as ms since the epoch
    int64_t sampleMs() {
        struct tm t = {};
        t.tm_year = 2026 - 1900; t.tm_mon = 9; t.tm_mday = 7;
        t.tm_hour = 12; t.tm_min = 34; t.tm_sec = 56; t.tm_isdst = -1;
        return static_cast<int64_t>(mktime(&t)) * 1000 + 789;
    }

public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Debug Log Tests");

        suite->addTest("File name is the creation date and time with a .csv extension", [this]() {
            ASSERT_STR_EQ(DebugLog::fileNameFor(sampleMs()), "2026-10-07_12-34-56.csv");
            return true;
        });

        suite->addTest("Row is time, time_ms, counters, total and trip", [this]() {
            int64_t ms = sampleMs();
            std::string expected = "2026-10-07 12:34:56.789," + std::to_string(ms) + ",4000000000,17,1234,-70";
            ASSERT_STR_EQ(DebugLog::formatRow(ms, 4000000000u, 17, 1234, -70), expected);
            return true;
        });

        suite->addTest("Start creates the directory and file with the header; rows wait for a full buffer", [this]() {
            DebugLog log;
            ASSERT_TRUE(log.start(dir, sampleMs()));
            std::string path = log.path();
            ASSERT_STR_EQ(path, dir + "/2026-10-07_12-34-56.csv");
            for (int i = 0; i < 99; ++i) log.record(sampleMs() + i, i, i, i, i);
            auto lines = readLines(path);
            ASSERT_EQ(lines.size(), (size_t)1);
            ASSERT_STR_EQ(lines[0], "time,time_ms,cntr1,cntr2,total_m,trip_m");
            ASSERT_EQ(log.buffered(), (size_t)99);
            log.stop();
            cleanup(path);
            return true;
        });

        suite->addTest("The 100th row writes all 100 rows in one go", [this]() {
            DebugLog log;
            ASSERT_TRUE(log.start(dir, sampleMs()));
            std::string path = log.path();
            for (int i = 0; i < 100; ++i) log.record(sampleMs() + i, i, i, i, i);
            ASSERT_EQ(log.buffered(), (size_t)0);
            auto lines = readLines(path);
            ASSERT_EQ(lines.size(), (size_t)101);
            log.stop();
            cleanup(path);
            return true;
        });

        suite->addTest("Stop writes the buffered tail and ignores further rows", [this]() {
            DebugLog log;
            ASSERT_TRUE(log.start(dir, sampleMs()));
            std::string path = log.path();
            for (int i = 0; i < 150; ++i) log.record(sampleMs() + i, i, i, i, i);
            log.stop();
            ASSERT_FALSE(log.isRunning());
            log.record(sampleMs(), 1, 2, 3, 4);
            auto lines = readLines(path);
            ASSERT_EQ(lines.size(), (size_t)151);
            cleanup(path);
            return true;
        });

        suite->addTest("Download name check accepts a log name and refuses anything else", [this]() {
            ASSERT_TRUE(DebugLog::isSafeFileName("2026-10-09_17-49-36.csv"));
            ASSERT_FALSE(DebugLog::isSafeFileName("../rally_config.json"));
            ASSERT_FALSE(DebugLog::isSafeFileName("..%2F.csv"));
            ASSERT_FALSE(DebugLog::isSafeFileName("a/b.csv"));
            ASSERT_FALSE(DebugLog::isSafeFileName("x.txt"));
            ASSERT_FALSE(DebugLog::isSafeFileName(".csv"));
            ASSERT_FALSE(DebugLog::isSafeFileName(""));
            return true;
        });

        suite->addTest("Listing is newest first with size, skipping non-csv; missing directory lists nothing", [this]() {
            ASSERT_EQ(DebugLog::listFiles("no_such_dir_here").size(), (size_t)0);
            mkdir(dir.c_str(), 0755);
            std::string older = dir + "/2026-10-01_08-00-00.csv";
            std::string newer = dir + "/2026-10-02_08-00-00.csv";
            std::string other = dir + "/notes.txt";
            { std::ofstream(older) << "abc"; }
            { std::ofstream(newer) << "abcdef"; }
            { std::ofstream(other) << "x"; }
            touch(older, 1000);
            touch(newer, 2000);
            auto files = DebugLog::listFiles(dir);
            ASSERT_EQ(files.size(), (size_t)2);
            ASSERT_STR_EQ(files[0].name, "2026-10-02_08-00-00.csv");
            ASSERT_EQ(files[0].size, (uint64_t)6);
            ASSERT_STR_EQ(files[1].name, "2026-10-01_08-00-00.csv");
            ASSERT_EQ(files[1].size, (uint64_t)3);
            std::remove(older.c_str()); std::remove(newer.c_str()); std::remove(other.c_str());
            cleanup("");
            return true;
        });

        suite->addTest("Purge deletes csv files older than 14 days and keeps the rest", [this]() {
            ASSERT_EQ(DebugLog::purgeOlderThan("no_such_dir_here", sampleMs(), 14), 0);
            mkdir(dir.c_str(), 0755);
            const int64_t day = 24LL * 60 * 60;
            const int64_t now_s = sampleMs() / 1000;
            std::string old_log = dir + "/2026-09-20_08-00-00.csv";
            std::string recent = dir + "/2026-10-01_08-00-00.csv";
            std::string other = dir + "/old_notes.txt";
            { std::ofstream(old_log) << "a"; }
            { std::ofstream(recent) << "a"; }
            { std::ofstream(other) << "a"; }
            touch(old_log, now_s - 15 * day);
            touch(recent, now_s - 13 * day);
            touch(other, now_s - 100 * day);
            ASSERT_EQ(DebugLog::purgeOlderThan(dir, sampleMs(), 14), 1);
            struct stat st;
            ASSERT_TRUE(stat(old_log.c_str(), &st) != 0);
            ASSERT_TRUE(stat(recent.c_str(), &st) == 0);
            ASSERT_TRUE(stat(other.c_str(), &st) == 0);
            std::remove(recent.c_str()); std::remove(other.c_str());
            cleanup("");
            return true;
        });

        suite->addTest("Not started: record does nothing and stop is harmless", [this]() {
            DebugLog log;
            log.record(sampleMs(), 1, 2, 3, 4);
            log.stop();
            ASSERT_FALSE(log.isRunning());
            ASSERT_EQ(log.buffered(), (size_t)0);
            return true;
        });

        return suite;
    }
};

#endif // TEST_DEBUG_LOG_H
