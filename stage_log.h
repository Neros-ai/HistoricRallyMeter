#ifndef STAGE_LOG_H
#define STAGE_LOG_H

#include <cstdint>
#include <string>
#include <vector>

// One CSV per stage: once a second, the time, Total distance, speed and
// ahead/behind. Opened when a stage starts, closed when it ends or is aborted.
// Rows are buffered ten at a time. Listing, purging and the safe-download name
// check are DebugLog's (the two logs share one directory and one retention).
class StageLog {
public:
    static constexpr size_t BUFFER_ROWS = 10;

    // Create logs_dir if needed and open logs/stage<N>_yyyy-mm-dd_hh-mm-ss.csv
    // (a "-2", "-3"... is added if that name is taken, so two starts in one
    // second never share a file), with the header line. False if the file
    // could not be created; the log is then stopped and record() does nothing.
    bool start(const std::string& logs_dir, int64_t now_ms, int stage_number);

    // Buffer one row; writes the buffer out when it reaches BUFFER_ROWS.
    void record(int64_t system_ms, long distance_m, double speed_kph, double ahead_behind_s);

    // Write what is buffered and close the file.
    // time_taken (surrey): written as a last "time taken,<text>" line when not empty.
    void stop(const std::string& time_taken = "");

    // A finish press after the stage ended on distance (surrey): the "time taken" line of the
    // log just closed is replaced with the press's figure. Does nothing while a log is running,
    // when none has been closed since the app started, or when time_taken is empty.
    void amendTimeTaken(const std::string& time_taken);

    bool isRunning() const { return running; }
    const std::string& path() const { return file_path; }
    size_t buffered() const { return buffer.size(); }

    // "yyyy-mm-dd hh:mm:ss.mmm,<ms>,<m>,<speed 1dp>,<signed ahead/behind 1dp>"
    static std::string formatRow(int64_t system_ms, long distance_m, double speed_kph,
                                 double ahead_behind_s);

    ~StageLog() { stop(); }

private:
    void flush();

    bool running = false;
    std::string file_path;
    std::string closed_path;   // the last log stopped, for amendTimeTaken
    std::vector<std::string> buffer;
};

// Which list the phone shows a log under: "stage" for stage<N>_ files, else "sensor".
std::string logKind(const std::string& name);

#endif // STAGE_LOG_H
