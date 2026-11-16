#include "config_saver.h"

#include <iostream>
#include <utility>

void SaveSchedule::mark(int64_t now_ms) {
    if (!dirty_) {
        dirty_ = true;
        first_ms_ = now_ms;
    }
    last_ms_ = now_ms;
}

bool SaveSchedule::due(int64_t now_ms) const {
    if (!dirty_) return false;
    return now_ms - last_ms_ >= QUIET_MS || now_ms - first_ms_ >= MAX_DELAY_MS;
}

SnapshotWriter::SnapshotWriter(WriteFn write)
    : write_(std::move(write)), thread_(&SnapshotWriter::run, this) {}

SnapshotWriter::~SnapshotWriter() {
    flush();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_one();
    if (thread_.joinable()) thread_.join();
}

void SnapshotWriter::submit(std::string bytes) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_ = std::move(bytes);   // an older snapshot still waiting is replaced: latest wins
        has_pending_ = true;
        ++submitted_;
    }
    wake_.notify_one();
}

void SnapshotWriter::flush() {
    std::unique_lock<std::mutex> lock(mutex_);
    done_.wait(lock, [this] { return handled_ >= submitted_; });
}

void SnapshotWriter::run() {
    for (;;) {
        std::string bytes;
        uint64_t number = 0;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return has_pending_ || stop_; });
            if (!has_pending_) return;   // stop, with nothing left to write
            bytes = std::move(pending_);
            pending_.clear();
            has_pending_ = false;
            number = submitted_;
        }
        // Outside the lock: submit() never waits for the card.
        if (!have_last_written_ || bytes != last_written_) {
            bool ok = false;
            try {
                ok = write_(bytes);
            } catch (...) {
                ok = false;
            }
            // One line when writes start failing and one when they recover, not one per retry.
            if (!ok && last_ok_.exchange(false))
                std::cerr << "[WARN] Config save failed: the config file was not written (the next save retries)" << std::endl;
            else if (ok && !last_ok_.exchange(true))
                std::cerr << "[INFO] Config save recovered: the config file is written again" << std::endl;
            if (ok) {
                last_written_ = std::move(bytes);
                have_last_written_ = true;
            } else {
                have_last_written_ = false;   // the next snapshot is written whatever it holds
            }
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            handled_ = number;
        }
        done_.notify_all();
    }
}

ConfigSaver::ConfigSaver(SerialiseFn serialise, SnapshotWriter::WriteFn write)
    : serialise_(std::move(serialise)), writer_(std::move(write)) {}

void ConfigSaver::saveNow() {
    // Snapshot first: if serialising throws, the deferred-change mark is still there for the next poll/flush.
    std::string snapshot = serialise_();
    schedule_.clear();   // this snapshot carries every deferred change too
    writer_.submit(std::move(snapshot));
}

void ConfigSaver::saveNowAndWait() {
    saveNow();
    writer_.flush();
}

void ConfigSaver::markDirty(int64_t now_ms) {
    schedule_.mark(now_ms);
}

void ConfigSaver::poll(int64_t now_ms) {
    if (schedule_.due(now_ms)) saveNow();
}

void ConfigSaver::flush() {
    if (schedule_.dirty()) saveNow();
    writer_.flush();
}
