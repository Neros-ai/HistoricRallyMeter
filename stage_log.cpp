#include "stage_log.h"
#include "debug_log.h"

#include <cstdio>
#include <ctime>
#include <fstream>
#include <sys/stat.h>

static const char* STAGE_HEADER = "time,time_ms,distance_m,speed_kph,ahead_behind_s\n";

std::string StageLog::formatRow(int64_t system_ms, long distance_m, double speed_kph,
                                double ahead_behind_s) {
    time_t secs = system_ms / 1000;
    struct tm t;
    localtime_r(&secs, &t);
    char buf[160];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d,%lld,%ld,%.1f,%+.1f",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec,
             static_cast<int>(system_ms % 1000), static_cast<long long>(system_ms),
             distance_m, speed_kph, ahead_behind_s);
    return buf;
}

static bool exists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool StageLog::start(const std::string& logs_dir, int64_t now_ms, int stage_number) {
    stop();
    closed_path.clear();
    mkdir(logs_dir.c_str(), 0755);   // fine if it already exists
    // DebugLog::fileNameFor gives "yyyy-mm-dd_hh-mm-ss.csv".
    const std::string stem = "stage" + std::to_string(stage_number) + "_" + DebugLog::fileNameFor(now_ms);
    std::string name = stem;
    for (int n = 2; exists(logs_dir + "/" + name) && n < 1000; n++)
        name = stem.substr(0, stem.size() - 4) + "-" + std::to_string(n) + ".csv";
    file_path = logs_dir + "/" + name;
    std::ofstream out(file_path, std::ios::out | std::ios::trunc);
    if (!out) {
        fprintf(stderr, "Stage log: cannot create %s\n", file_path.c_str());
        file_path.clear();
        return false;
    }
    out << STAGE_HEADER;
    buffer.clear();
    running = true;
    return true;
}

void StageLog::record(int64_t system_ms, long distance_m, double speed_kph, double ahead_behind_s) {
    if (!running) return;
    buffer.push_back(formatRow(system_ms, distance_m, speed_kph, ahead_behind_s));
    if (buffer.size() >= BUFFER_ROWS) flush();
}

void StageLog::flush() {
    if (buffer.empty() || file_path.empty()) return;
    std::ofstream out(file_path, std::ios::out | std::ios::app);
    if (out) {
        for (const std::string& row : buffer) out << row << '\n';
    } else {
        fprintf(stderr, "Stage log: cannot append to %s\n", file_path.c_str());
    }
    buffer.clear();
}

void StageLog::stop(const std::string& time_taken) {
    if (!running) return;
    flush();
    if (!time_taken.empty() && !file_path.empty()) {
        std::ofstream out(file_path, std::ios::out | std::ios::app);
        if (out) out << "time taken," << time_taken << '\n';
    }
    running = false;
    closed_path = file_path;
    file_path.clear();
}

// The closed log without its "time taken" line, written beside it and renamed over it.
static bool rewriteWithoutTimeTaken(const std::string& path, const std::string& time_taken) {
    std::vector<std::string> rows;
    {
        std::ifstream in(path);
        if (!in) return false;
        std::string row;
        while (std::getline(in, row))
            if (row.compare(0, 11, "time taken,") != 0) rows.push_back(row);
    }
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::out | std::ios::trunc);
        if (!out) return false;
        for (const std::string& row : rows) out << row << '\n';
        if (!time_taken.empty()) out << "time taken," << time_taken << '\n';
        if (!out.flush()) { std::remove(tmp.c_str()); return false; }
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) { std::remove(tmp.c_str()); return false; }
    return true;
}

bool StageLog::resume() {
    if (running || closed_path.empty()) return false;
    if (!rewriteWithoutTimeTaken(closed_path, "")) return false;
    file_path = closed_path;
    closed_path.clear();
    buffer.clear();
    running = true;
    return true;
}

void StageLog::amendTimeTaken(const std::string& time_taken) {
    if (running || closed_path.empty() || time_taken.empty()) return;
    rewriteWithoutTimeTaken(closed_path, time_taken);
}

std::string logKind(const std::string& name) {
    return name.compare(0, 5, "stage") == 0 ? "stage" : "sensor";
}
