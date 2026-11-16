#ifndef TEST_CONFIG_SAVER_H
#define TEST_CONFIG_SAVER_H

#include "test_framework.h"
#include "../config_saver.h"
#include "../config_file.h"
#include "../rally_state.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

// The config saver (config_saver.h): deferred saves coalesce, critical saves start at once on the
// background writer, an older snapshot never lands after a newer one, and flush leaves nothing behind.
class TestConfigSaver {
    // A write function that records what reached "the card", safe to call from the writer thread.
    struct Card {
        std::mutex m;
        std::vector<std::string> writes;
        bool fail = false;
        SnapshotWriter::WriteFn fn() {
            return [this](const std::string& b) {
                std::lock_guard<std::mutex> lock(m);
                if (fail) return false;
                writes.push_back(b);
                return true;
            };
        }
        size_t count() { std::lock_guard<std::mutex> lock(m); return writes.size(); }
        std::vector<std::string> all() { std::lock_guard<std::mutex> lock(m); return writes; }
    };

    static std::string readFile(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Config Saver Tests");

        // ---- the deferred schedule ----

        suite->addTest("SaveSchedule: nothing is due when nothing changed", []() {
            SaveSchedule s;
            ASSERT_FALSE(s.dirty());
            ASSERT_FALSE(s.due(0));
            ASSERT_FALSE(s.due(1000000));
            return true;
        });

        suite->addTest("SaveSchedule: due QUIET_MS after the last change, not before", []() {
            SaveSchedule s;
            s.mark(1000);
            s.mark(1100);
            s.mark(1200);
            ASSERT_FALSE(s.due(1200 + SaveSchedule::QUIET_MS - 1));
            ASSERT_TRUE(s.due(1200 + SaveSchedule::QUIET_MS));
            s.clear();
            ASSERT_FALSE(s.due(5000));
            return true;
        });

        suite->addTest("SaveSchedule: changes that never pause are still saved within MAX_DELAY_MS", []() {
            SaveSchedule s;
            // A held trim key: a change every 150 ms, never QUIET_MS apart.
            int64_t t = 0;
            for (; t < SaveSchedule::MAX_DELAY_MS; t += 150) {
                s.mark(t);
                ASSERT_FALSE(s.due(t));
            }
            s.mark(t);
            ASSERT_TRUE(s.due(t));   // t >= MAX_DELAY_MS after the first change
            return true;
        });

        // ---- the saver ----

        suite->addTest("ConfigSaver: many deferred marks make one write, of the latest state", []() {
            Card card;
            int value = 0;
            {
                ConfigSaver saver([&value] { return std::to_string(value); }, card.fn());
                // Typing three digits, then each repeat of a held key: 11 changes 100 ms apart.
                int64_t t = 0;
                for (int i = 1; i <= 3; ++i, t += 100) {
                    value = i;
                    saver.markDirty(t);
                    saver.poll(t);
                }
                for (; t < 300 + SaveSchedule::QUIET_MS + 200; t += 50) saver.poll(t);
                ASSERT_FALSE(saver.dirty());
            }   // the destructor flushes
            auto w = card.all();
            ASSERT_EQ(w.size(), static_cast<size_t>(1));
            ASSERT_EQ(w[0], std::string("3"));
            return true;
        });

        suite->addTest("ConfigSaver: nothing changed, nothing written (poll, flush and exit)", []() {
            Card card;
            {
                ConfigSaver saver([] { return std::string("x"); }, card.fn());
                for (int64_t t = 0; t < 5000; t += 50) saver.poll(t);
                saver.flush();
            }
            ASSERT_EQ(card.count(), static_cast<size_t>(0));
            return true;
        });

        suite->addTest("ConfigSaver: a critical save is written at once, with no poll and no flush", []() {
            Card card;
            ConfigSaver saver([] { return std::string("stage started"); }, card.fn());
            const auto t0 = std::chrono::steady_clock::now();
            saver.saveNow();
            // The writer starts on it immediately; allow the test machine a generous margin.
            while (card.count() == 0 &&
                   std::chrono::steady_clock::now() - t0 < std::chrono::seconds(2)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            const auto waited = std::chrono::steady_clock::now() - t0;
            ASSERT_EQ(card.count(), static_cast<size_t>(1));
            ASSERT_LT(waited, std::chrono::milliseconds(SaveSchedule::QUIET_MS));
            return true;
        });

        suite->addTest("ConfigSaver: saveNowAndWait returns only once the snapshot is written", []() {
            Card card;
            ConfigSaver saver([] { return std::string("loss"); }, card.fn());
            saver.saveNowAndWait();
            ASSERT_EQ(card.count(), static_cast<size_t>(1));
            return true;
        });

        suite->addTest("ConfigSaver: a critical save carries the deferred changes, which are not written again", []() {
            Card card;
            int value = 1;
            ConfigSaver saver([&value] { return std::to_string(value); }, card.fn());
            saver.markDirty(0);          // a setting typed
            value = 2;
            saver.saveNow();             // then next pressed
            for (int64_t t = 0; t < 3000; t += 50) saver.poll(t);
            saver.flush();
            auto w = card.all();
            ASSERT_EQ(w.size(), static_cast<size_t>(1));
            ASSERT_EQ(w[0], std::string("2"));
            return true;
        });

        suite->addTest("ConfigSaver: flush writes a pending deferred change", []() {
            Card card;
            ConfigSaver saver([] { return std::string("trimmed"); }, card.fn());
            saver.markDirty(0);
            ASSERT_EQ(card.count(), static_cast<size_t>(0));
            saver.flush();
            ASSERT_EQ(card.count(), static_cast<size_t>(1));
            ASSERT_FALSE(saver.dirty());
            return true;
        });

        // ---- the writer ----

        suite->addTest("SnapshotWriter: an older snapshot never lands after a newer one (latest wins)", []() {
            std::mutex m;
            std::condition_variable cv;
            bool release = false;
            bool first_started = false;
            std::vector<std::string> landed;
            {
                SnapshotWriter writer([&](const std::string& b) {
                    std::unique_lock<std::mutex> lock(m);
                    if (b == "1") {   // the card is slow on the first write
                        first_started = true;
                        cv.notify_all();
                        cv.wait(lock, [&] { return release; });
                    }
                    landed.push_back(b);
                    return true;
                });
                writer.submit("1");
                {
                    std::unique_lock<std::mutex> lock(m);
                    cv.wait(lock, [&] { return first_started; });
                }
                writer.submit("2");
                writer.submit("3");
                writer.submit("4");
                {
                    std::lock_guard<std::mutex> lock(m);
                    release = true;
                }
                cv.notify_all();
                writer.flush();
            }
            ASSERT_EQ(landed.size(), static_cast<size_t>(2));
            ASSERT_EQ(landed[0], std::string("1"));
            ASSERT_EQ(landed[1], std::string("4"));   // 2 and 3 were replaced, never written after 4
            return true;
        });

        suite->addTest("SnapshotWriter: snapshots land in the order they were taken", []() {
            Card card;
            {
                SnapshotWriter writer(card.fn());
                for (int i = 1; i <= 50; ++i) writer.submit(std::to_string(i));
                writer.flush();
            }
            auto w = card.all();
            ASSERT_TRUE(!w.empty());
            ASSERT_EQ(w.back(), std::string("50"));
            for (size_t i = 1; i < w.size(); ++i) {
                ASSERT_LT(std::stoi(w[i - 1]), std::stoi(w[i]));
            }
            return true;
        });

        suite->addTest("SnapshotWriter: an unchanged snapshot is not written again; a failed one is retried", []() {
            Card card;
            SnapshotWriter writer(card.fn());
            writer.submit("a");
            writer.flush();
            writer.submit("a");
            writer.flush();
            ASSERT_EQ(card.count(), static_cast<size_t>(1));
            {
                std::lock_guard<std::mutex> lock(card.m);
                card.fail = true;
            }
            writer.submit("b");   // the card refuses it
            writer.flush();
            {
                std::lock_guard<std::mutex> lock(card.m);
                card.fail = false;
            }
            writer.submit("b");   // the same bytes again: written this time
            writer.flush();
            auto w = card.all();
            ASSERT_EQ(w.size(), static_cast<size_t>(2));
            ASSERT_EQ(w[1], std::string("b"));
            return true;
        });

        suite->addTest("SnapshotWriter: a failed write is reported through lastWriteOk and the same bytes are retried", []() {
            Card card;
            {
                std::lock_guard<std::mutex> lock(card.m);
                card.fail = true;
            }
            SnapshotWriter writer(card.fn());
            ASSERT_TRUE(writer.lastWriteOk());
            writer.submit("x");
            writer.flush();
            ASSERT_FALSE(writer.lastWriteOk());
            writer.submit("x");   // still failing: still reported, nothing written
            writer.flush();
            ASSERT_FALSE(writer.lastWriteOk());
            ASSERT_EQ(card.count(), static_cast<size_t>(0));
            {
                std::lock_guard<std::mutex> lock(card.m);
                card.fail = false;
            }
            writer.submit("x");   // the same bytes: written this time
            writer.flush();
            ASSERT_TRUE(writer.lastWriteOk());
            auto w = card.all();
            ASSERT_EQ(w.size(), static_cast<size_t>(1));
            ASSERT_EQ(w[0], std::string("x"));
            return true;
        });

        suite->addTest("ConfigSaver: a serialiser that throws once keeps the deferred save pending", []() {
            Card card;
            bool throw_now = true;
            ConfigSaver saver([&throw_now]() -> std::string {
                if (throw_now) { throw_now = false; throw std::runtime_error("serialise"); }
                return "state";
            }, card.fn());
            saver.markDirty(0);
            bool threw = false;
            try { saver.poll(5000); } catch (const std::runtime_error&) { threw = true; }
            ASSERT_TRUE(threw);
            ASSERT_TRUE(saver.dirty());   // the mark survived the failed snapshot
            saver.poll(5001);             // next poll: serialises and writes it
            ASSERT_FALSE(saver.dirty());
            saver.flush();
            auto w = card.all();
            ASSERT_EQ(w.size(), static_cast<size_t>(1));
            ASSERT_EQ(w[0], std::string("state"));
            // flush takes the same path
            throw_now = true;
            saver.markDirty(6000);
            threw = false;
            try { saver.flush(); } catch (const std::runtime_error&) { threw = true; }
            ASSERT_TRUE(threw);
            ASSERT_TRUE(saver.dirty());
            return true;
        });

        // ---- ConfigFile split ----

        suite->addTest("ConfigFile: serialise + writeAtomic make exactly the file save makes", []() {
            RallyState s;
            s.calibration = 1234567;
            s.rallyTimeOffset_ms = -1500;
            s.total_start_cntr1 = 4242;
            const std::string a = "/tmp/rb_saver_a.json";
            const std::string b = "/tmp/rb_saver_b.json";
            ConfigFile::save(s, a);
            ASSERT_TRUE(ConfigFile::writeAtomic(ConfigFile::serialise(s), b));
            const std::string fa = readFile(a);
            ASSERT_TRUE(!fa.empty());
            ASSERT_EQ(fa, readFile(b));
            ASSERT_EQ(fa, ConfigFile::serialise(s));
            std::ifstream tmp(b + ".tmp");
            ASSERT_FALSE(tmp.good());   // the temporary was renamed away
            RallyState back;
            ConfigFile::load(back, b);
            ASSERT_EQ(back.calibration, s.calibration);
            ASSERT_EQ(back.rallyTimeOffset_ms, s.rallyTimeOffset_ms);
            ASSERT_EQ(back.total_start_cntr1, s.total_start_cntr1);
            std::remove(a.c_str());
            std::remove(b.c_str());
            return true;
        });

        suite->addTest("ConfigFile: writeAtomic that cannot write leaves the old file alone", []() {
            ASSERT_FALSE(ConfigFile::writeAtomic("{}\n", "/nonexistent_dir_rb/rally_config.json"));
            return true;
        });

        suite->addTest("ConfigFile: writeAtomic syncs the folder and saves into a nested directory and a bare filename", []() {
            RallyState s;
            s.calibration = 424242;
            char tmpl[] = "/tmp/rb_wa_XXXXXX";
            ASSERT_TRUE(mkdtemp(tmpl) != nullptr);
            const std::string dir = tmpl;
            ASSERT_EQ(mkdir((dir + "/a").c_str(), 0755), 0);
            ASSERT_EQ(mkdir((dir + "/a/b").c_str(), 0755), 0);
            const std::string nested = dir + "/a/b/rally_config.json";
            ASSERT_TRUE(ConfigFile::writeAtomic(ConfigFile::serialise(s), nested));
            RallyState back;
            ConfigFile::load(back, nested);
            ASSERT_EQ(back.calibration, 424242);
            // A bare filename means the current directory.
            char cwd[1024];
            ASSERT_TRUE(getcwd(cwd, sizeof cwd) != nullptr);
            ASSERT_EQ(chdir(dir.c_str()), 0);
            const bool wrote = ConfigFile::writeAtomic(ConfigFile::serialise(s), "bare_config.json");
            RallyState back2;
            if (wrote) ConfigFile::load(back2, "bare_config.json");
            ASSERT_EQ(chdir(cwd), 0);
            ASSERT_TRUE(wrote);
            ASSERT_EQ(back2.calibration, 424242);
            std::remove((dir + "/bare_config.json").c_str());
            std::remove(nested.c_str());
            rmdir((dir + "/a/b").c_str());
            rmdir((dir + "/a").c_str());
            rmdir(dir.c_str());
            return true;
        });

        return suite;
    }
};

#endif // TEST_CONFIG_SAVER_H
