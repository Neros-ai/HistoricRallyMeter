#include "debug_log.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <dirent.h>
#include <sys/stat.h>

static const char* HEADER = "time,time_ms,cntr1,cntr2,total_m,trip_m\n";

std::string DebugLog::fileNameFor(int64_t now_ms) {
    time_t secs = now_ms / 1000;
    struct tm t;
    localtime_r(&secs, &t);
    char buf[40];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d_%02d-%02d-%02d.csv",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
    return buf;
}

std::string DebugLog::formatRow(int64_t system_ms, uint32_t cntr1, uint32_t cntr2,
                                long total_m, long trip_m) {
    time_t secs = system_ms / 1000;
    struct tm t;
    localtime_r(&secs, &t);
    char buf[128];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d,%lld,%u,%u,%ld,%ld",
             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec,
             static_cast<int>(system_ms % 1000),
             static_cast<long long>(system_ms), cntr1, cntr2, total_m, trip_m);
    return buf;
}

bool DebugLog::start(const std::string& logs_dir, int64_t now_ms) {
    stop();
    mkdir(logs_dir.c_str(), 0755);   // fine if it already exists
    file_path = logs_dir + "/" + fileNameFor(now_ms);
    std::ofstream out(file_path, std::ios::out | std::ios::trunc);
    if (!out) {
        fprintf(stderr, "Debug log: cannot create %s\n", file_path.c_str());
        file_path.clear();
        return false;
    }
    out << HEADER;
    buffer.clear();
    buffer.reserve(BUFFER_ROWS);
    running = true;
    return true;
}

void DebugLog::record(int64_t system_ms, uint32_t cntr1, uint32_t cntr2, long total_m, long trip_m) {
    if (!running) return;
    buffer.push_back(formatRow(system_ms, cntr1, cntr2, total_m, trip_m));
    if (buffer.size() >= BUFFER_ROWS) flush();
}

void DebugLog::flush() {
    if (buffer.empty() || file_path.empty()) return;
    std::ofstream out(file_path, std::ios::out | std::ios::app);
    if (out) {
        for (const std::string& row : buffer) out << row << '\n';
    } else {
        fprintf(stderr, "Debug log: cannot append to %s\n", file_path.c_str());
    }
    buffer.clear();
}

void DebugLog::stop() {
    if (!running) return;
    flush();
    running = false;
    file_path.clear();
}

bool DebugLog::isSafeFileName(const std::string& name) {
    if (name.size() < 5 || name.size() > 128) return false;
    if (name.compare(name.size() - 4, 4, ".csv") != 0) return false;
    if (name[0] == '.') return false;
    for (char c : name) {
        if (!(isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')) return false;
    }
    return true;
}

std::vector<DebugLog::Entry> DebugLog::listFiles(const std::string& logs_dir) {
    std::vector<Entry> out;
    DIR* d = opendir(logs_dir.c_str());
    if (!d) return out;
    while (struct dirent* e = readdir(d)) {
        std::string name = e->d_name;
        if (!isSafeFileName(name)) continue;
        struct stat st;
        if (stat((logs_dir + "/" + name).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        Entry entry;
        entry.name = name;
        entry.size = static_cast<uint64_t>(st.st_size);
        entry.modified_ms = static_cast<int64_t>(st.st_mtim.tv_sec) * 1000 + st.st_mtim.tv_nsec / 1000000;
        out.push_back(entry);
    }
    closedir(d);
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        if (a.modified_ms != b.modified_ms) return a.modified_ms > b.modified_ms;
        return a.name > b.name;
    });
    return out;
}

int DebugLog::purgeOlderThan(const std::string& logs_dir, int64_t now_ms, int max_age_days) {
    const int64_t cutoff_ms = now_ms - static_cast<int64_t>(max_age_days) * 24 * 60 * 60 * 1000;
    int deleted = 0;
    for (const Entry& e : listFiles(logs_dir)) {
        if (e.modified_ms >= cutoff_ms) continue;
        if (std::remove((logs_dir + "/" + e.name).c_str()) == 0) deleted++;
    }
    return deleted;
}
