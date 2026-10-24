#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <cstdint>
#include <string>
#include <vector>

// CSV log of every counter poll, for chasing RF interference and sensor
// faults. Rows are buffered and written 100 at a time so the 10 ms polling
// loop is not slowed by a file write per poll. See Design.md "Debug logs".
class DebugLog {
public:
    static constexpr size_t BUFFER_ROWS = 100;

    // Create logs_dir if needed and open a new file in it named for now:
    // yyyy-mm-dd_hh-mm-ss.csv, with the header line. Returns false if the
    // file could not be opened. now_ms is system time in milliseconds.
    bool start(const std::string& logs_dir, int64_t now_ms);

    // Record one poll. Writes the buffer to the file when it reaches
    // BUFFER_ROWS. Does nothing when not started.
    void record(int64_t system_ms, uint32_t cntr1, uint32_t cntr2, long total_m, long trip_m);

    // Write whatever is buffered and close the file.
    void stop();

    bool isRunning() const { return running; }
    const std::string& path() const { return file_path; }
    size_t buffered() const { return buffer.size(); }

    // The CSV row for one poll (no newline), and the file name for a start time.
    static std::string formatRow(int64_t system_ms, uint32_t cntr1, uint32_t cntr2, long total_m, long trip_m);
    static std::string fileNameFor(int64_t now_ms);

    // One log file as listed for the phone app.
    struct Entry {
        std::string name;
        uint64_t size = 0;
        int64_t modified_ms = 0;
    };

    // The .csv files in logs_dir, newest first. Empty if the directory is missing.
    static std::vector<Entry> listFiles(const std::string& logs_dir);

    // Delete the .csv files in logs_dir last modified more than max_age_days
    // before now_ms. Returns how many were deleted.
    static int purgeOlderThan(const std::string& logs_dir, int64_t now_ms, int max_age_days);

    // True for a bare file name the server may send: letters, digits, '-',
    // '_' and '.', ending in ".csv", so nothing outside logs_dir is reachable.
    static bool isSafeFileName(const std::string& name);

    ~DebugLog() { stop(); }

private:
    void flush();

    bool running = false;
    std::string file_path;
    std::vector<std::string> buffer;
};

#endif // DEBUG_LOG_H
