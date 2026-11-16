#ifndef CONFIG_SAVER_H
#define CONFIG_SAVER_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

// Saving the config without stalling the 10 ms loop on the SD card.
//
// The GTK thread takes a complete snapshot of the state (ConfigFile::serialise, memory only) and
// hands the bytes to one background thread that does the atomic write + fsync. Only that thread
// touches the file once the app is running, so two writes never share the .tmp file.
//
// Two kinds of save:
//  - saveNow: a rally-critical change (stage start/stop/abort, next/prev/auto-advance, autostart,
//    distance sets/resets, calibration, rally clock set...). The snapshot is taken at the call and
//    the writer starts on it at once, exactly when the synchronous save used to start: it reaches
//    the card as soon as it did before, but the loop no longer waits for it.
//  - markDirty: a high-frequency, non-critical change (trim-key repeat, a settings field typed a
//    key at a time, settings toggles). One snapshot is written QUIET_MS after the last change, and
//    never later than MAX_DELAY_MS after the first unsaved one, whatever keeps changing.
//
// Ordering: snapshots are written in the order they were taken; a newer snapshot submitted while
// an older one waits replaces it (latest wins), so an older snapshot never lands after a newer.

// When a deferred save is due. No clock of its own: the caller passes a monotonic now.
class SaveSchedule {
public:
    static constexpr int64_t QUIET_MS = 300;       // after the last change
    static constexpr int64_t MAX_DELAY_MS = 1000;  // after the first change not yet saved

    void mark(int64_t now_ms);
    bool dirty() const { return dirty_; }
    bool due(int64_t now_ms) const;
    void clear() { dirty_ = false; }

private:
    bool dirty_ = false;
    int64_t first_ms_ = 0;
    int64_t last_ms_ = 0;
};

// One background writer. submit() never blocks on the write; flush() waits until every snapshot
// submitted so far has been written (or found identical to the last one written).
class SnapshotWriter {
public:
    using WriteFn = std::function<bool(const std::string& bytes)>;

    explicit SnapshotWriter(WriteFn write);
    ~SnapshotWriter();   // flushes, then stops the thread
    SnapshotWriter(const SnapshotWriter&) = delete;
    SnapshotWriter& operator=(const SnapshotWriter&) = delete;

    void submit(std::string bytes);
    void flush();
    bool lastWriteOk() const { return last_ok_; }   // false after a failed write, until one succeeds

private:
    void run();

    WriteFn write_;
    std::mutex mutex_;
    std::condition_variable wake_;   // the thread: something to write, or stop
    std::condition_variable done_;   // flush(): a snapshot has been handled
    std::string pending_;
    bool has_pending_ = false;
    uint64_t submitted_ = 0;   // snapshots submitted
    uint64_t handled_ = 0;     // number of the newest snapshot written (or skipped as unchanged)
    bool stop_ = false;
    // Thread-only: the bytes of the last successful write, so an unchanged snapshot is not written again.
    std::string last_written_;
    bool have_last_written_ = false;
    std::atomic<bool> last_ok_{true};
    std::thread thread_;   // last: started once everything above exists
};

// What the app calls.
class ConfigSaver {
public:
    using SerialiseFn = std::function<std::string()>;

    ConfigSaver(SerialiseFn serialise, SnapshotWriter::WriteFn write);
    ~ConfigSaver() { flush(); }   // the state serialise reads must outlive the saver
    ConfigSaver(const ConfigSaver&) = delete;
    ConfigSaver& operator=(const ConfigSaver&) = delete;

    void saveNow();          // critical: snapshot now, written at once
    void saveNowAndWait();   // as saveNow, and return only once it is on the card
    void markDirty(int64_t now_ms);   // deferred: written by poll() once due
    void poll(int64_t now_ms);        // writes the deferred snapshot when it is due
    void flush();            // exit: any deferred change now, and wait for the card
    bool dirty() const { return schedule_.dirty(); }

private:
    SerialiseFn serialise_;
    SaveSchedule schedule_;
    SnapshotWriter writer_;
};

#endif // CONFIG_SAVER_H
