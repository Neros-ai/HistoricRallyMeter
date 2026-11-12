#include "stage_rows.h"
#include "calculations.h"
#include "sector.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

std::vector<StageRow> buildStageRows(const std::vector<Segment>& segs,
                                     long first_number, double first_from_m) {
    std::vector<StageRow> rows;
    double from = first_from_m;
    double total = 0.0;
    for (size_t i = 0; i < segs.size(); i++) {
        StageRow r;
        r.number = (i == 0) ? first_number
                 : (segs[i].box >= 0 ? segs[i].box : rows.back().number + 1);
        r.from_m = from;
        r.dist_m = segs[i].distance_m;
        r.to_m = from + r.dist_m;
        r.chg = (i == 0) || segs[i].changesSpeed;
        r.kph = segs[i].target_speed_kph;
        r.ms = static_cast<long>(std::lround(r.kph / 3.6));
        r.auto_available = (i >= 1) && r.chg;
        r.auto_on = (i >= 1) && segs[i - 1].autoNext;
        r.time_s = (r.kph > 0.0) ? r.dist_m / (r.kph / 3.6) : 0.0;
        total += r.time_s;
        r.total_s = total;
        rows.push_back(r);
        from = r.to_m;
    }
    return rows;
}

DistanceParse parseStageDistances(const std::string& text, int unit) {
    const bool km = unit != DIST_M;   // a decimal figure: the comma is its decimal mark
    DistanceParse out;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t semi = text.find(';', pos);
        size_t end = (semi == std::string::npos) ? text.size() : semi;
        std::string tok;
        for (size_t i = pos; i < end; i++) {
            char c = text[i];
            if (c == ' ' || c == '\t') continue;
            if (c == ',') { if (km) tok += '.'; continue; }
            tok += c;
        }
        if (!tok.empty()) {
            char* stop = nullptr;
            double v = std::strtod(tok.c_str(), &stop);
            bool ok = stop && *stop == '\0' && v > 0.0;
            if (!ok) {
                out.rest = text.substr(pos);
                return out;
            }
            // km and metres: whole metres. Miles: to the millimetre, so a typed 1.234 reads back 1.234.
            out.accepted.push_back(distIsMiles(unit) ? std::round(v * METRES_PER_MILE * 1000.0) / 1000.0
                                                     : std::round(distIsKm(unit) ? v * 1000.0 : v));
        }
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
    return out;
}

void carrySpeeds(std::vector<Segment>& segs) {
    for (size_t i = 1; i < segs.size(); i++) {
        if (!segs[i].changesSpeed) {
            segs[i].target_speed_kph = segs[i - 1].target_speed_kph;
            segs[i - 1].autoNext = true;
        }
    }
}

std::vector<Segment> stageForDriving(const std::vector<Segment>& roadbook) {
    std::vector<Segment> segs = roadbook;
    carrySpeeds(segs);
    return segs;
}

void appendStageDistances(std::vector<Segment>& segs, double first_from_m,
                          const std::vector<double>& to_m) {
    double end = first_from_m;
    for (const Segment& s : segs) end += s.distance_m;
    for (double to : to_m) {
        if (to <= end) continue;
        Segment s;
        s.target_speed_kph = segs.empty() ? 0.0 : segs.back().target_speed_kph;
        s.distance_m = to - end;
        s.changesSpeed = !segs.empty() ? false : true;
        s.autoNext = true;
        if (!segs.empty()) segs.back().autoNext = true;
        segs.push_back(s);
        end = to;
    }
}

std::string insertStageDistances(std::vector<Segment>& segs, double first_from_m,
                                 const std::vector<double>& to_m, int km) {
    std::string refused;
    auto refuse = [&](double m) {
        if (!refused.empty()) refused += ", ";
        refused += formatDistance(m, km);
    };
    for (double to : to_m) {
        double from = first_from_m;
        bool done = false;
        if (to <= first_from_m) { refuse(to); continue; }
        for (size_t i = 0; i < segs.size() && !done; i++) {
            const double end = from + segs[i].distance_m;
            if (to == from || to == end) { refuse(to); done = true; }
            else if (to > from && to < end) {
                Segment second = segs[i];            // keeps the speed and the row's own auto
                second.distance_m = end - to;
                second.changesSpeed = false;
                second.box = -1;
                segs[i].distance_m = to - from;
                segs[i].autoNext = true;             // the new half starts by itself
                segs.insert(segs.begin() + static_cast<long>(i) + 1, second);
                done = true;
            }
            from = end;
        }
        if (!done) {
            std::vector<double> one = {to};
            appendStageDistances(segs, first_from_m, one);
        }
    }
    carrySpeeds(segs);
    if (refused.empty()) return "";
    return "Not added (already a point, or not past the start): " + refused + " " + distUnitName(km) + ".";
}

void setRowSpeed(std::vector<Segment>& segs, size_t row, double kph) {
    if (row >= segs.size()) return;
    segs[row].target_speed_kph = kph;
    carrySpeeds(segs);
}

void setRowChange(std::vector<Segment>& segs, size_t row, bool chg) {
    if (row == 0 || row >= segs.size()) return;
    segs[row].changesSpeed = chg;
    carrySpeeds(segs);
}

void setRowAuto(std::vector<Segment>& segs, size_t row, bool on) {
    if (row == 0 || row >= segs.size() || !segs[row].changesSpeed) return;
    segs[row - 1].autoNext = on;
}

bool setRowTo(std::vector<Segment>& segs, double first_from_m, size_t row, double new_to_m) {
    if (row >= segs.size()) return false;
    double from = first_from_m;
    for (size_t i = 0; i < row; i++) from += segs[i].distance_m;
    const double old_to = from + segs[row].distance_m;
    if (new_to_m <= from) return false;
    if (row + 1 < segs.size()) {
        const double next_to = old_to + segs[row + 1].distance_m;
        if (new_to_m >= next_to) return false;
        segs[row + 1].distance_m = next_to - new_to_m;
    }
    segs[row].distance_m = new_to_m - from;
    return true;
}

void deleteRow(std::vector<Segment>& segs, double& first_from_m, size_t row) {
    if (row >= segs.size()) return;
    const double len = segs[row].distance_m;
    if (row == 0) {
        first_from_m += len;
    } else {
        segs[row - 1].distance_m += len;
    }
    segs.erase(segs.begin() + static_cast<long>(row));
    // An empty stage has no distances to protect: back to 0, or deleting row by
    // row would leave the frame at the old end and refuse every new distance.
    if (segs.empty()) first_from_m = 0.0;
    if (!segs.empty()) segs[0].changesSpeed = true;
    carrySpeeds(segs);
}

std::vector<double> deriveWaypoints(const std::vector<Segment>& segs) {
    std::vector<double> out;
    double cum = 0.0;
    for (const Segment& s : segs) {
        cum += s.distance_m;
        out.push_back(cum);
    }
    return out;
}

std::string formatMmSs(double seconds) {
    long t = static_cast<long>(std::lround(seconds));
    if (t < 0) t = 0;
    char buf[24];
    snprintf(buf, sizeof(buf), "%ld:%02ld", t / 60, t % 60);
    return buf;
}

std::string formatHMmSs(double seconds) {
    long t = static_cast<long>(std::lround(seconds));
    if (t < 0) t = 0;
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld:%02ld:%02ld", t / 3600, (t / 60) % 60, t % 60);
    return buf;
}

const char* distUnitName(int unit) {
    return distIsMiles(unit) ? "mi" : distIsKm(unit) ? "km" : "m";
}

double distUnitToMetres(int unit, double figure) {
    return distIsMiles(unit) ? figure * METRES_PER_MILE : distIsKm(unit) ? figure * 1000.0 : figure;
}

std::string formatDistance(double metres, int unit) {
    char buf[32];
    if (unit == DIST_KM) {
        // Truncated to the 10 m, never rounded up: the figure must not read ahead of the car.
        snprintf(buf, sizeof(buf), "%.2f", std::trunc(metres / 10.0) / 100.0 + 0.0);   // + 0.0 turns -0 into 0
        return buf;
    }
    if (unit == DIST_MI || unit == DIST_MI2 || unit == DIST_KM3) {
        // Thousandths of the unit, cut down, never rounded up. Miles get half a metre of allowance,
        // because a mile figure does not fall on a whole millimetre: without it a typed 1.234 reads
        // back 1.233. Km to the metre needs none (whole metres first, as the metres figure rounds).
        const long long mm = std::llround(metres * 1000.0);
        const bool neg = mm < 0;
        const long long a = neg ? -mm : mm;
        long long thousandths = unit == DIST_KM3 ? std::llround(a / 1000.0)
                                                 : static_cast<long long>(std::floor((a + 500) / METRES_PER_MILE));
        // Two decimals when asked for (0.00 miles), and from 100 units up: six characters is all the big figures hold.
        if (unit == DIST_MI2 || thousandths >= 100000) {
            const long long hundredths = thousandths / 10;
            snprintf(buf, sizeof(buf), "%s%lld.%02lld", neg && hundredths > 0 ? "-" : "", hundredths / 100, hundredths % 100);
        } else {
            snprintf(buf, sizeof(buf), "%s%lld.%03lld", neg && thousandths > 0 ? "-" : "", thousandths / 1000, thousandths % 1000);
        }
        return buf;
    }
    return formatDistanceGrouped(static_cast<long>(std::lround(metres)));
}

SegRowText segRowText(const Segment& seg, const StageRow& row, size_t index, int rally_type, int km, bool mph) {
    SegRowText t;
    t.from = formatDistance(row.from_m, km);
    t.to = formatDistance(row.to_m, km);
    t.dist = formatDistance(row.dist_m, km);
    t.tot = formatHMmSs(row.total_s);
    char b[24];
    if (rally_type == 2) {
        t.num = std::to_string(index + 1);
        t.ref = (seg.type == SEG_NAVI || seg.type == SEG_LEG) ? "" : std::to_string(seg.ref);
        snprintf(b, sizeof(b), "%.2f", mph ? seg.target_speed_kph * 0.621371 : seg.target_speed_kph);
        t.kph = seg.target_speed_kph > 0 ? b : "";
        t.dur = seg.type == SEG_PC ? formatMmSs(seg.pc_duration_s) : formatHMmSs(row.time_s);
        return t;
    }
    t.num = std::to_string(row.number);
    snprintf(b, sizeof(b), "%g", mph ? std::round(row.kph * 0.621371 * 100.0) / 100.0 : row.kph);
    t.kph = row.chg ? (row.kph > 0 ? b : "") : "\"";   // ditto: no speed change on this row
    t.dur = formatHMmSs(row.time_s);
    return t;
}

std::string sectorAverageText(const std::vector<Segment>& segs, long sector_duration_s, bool mph) {
    double m = 0.0;
    for (const Segment& sg : segs) m += sg.distance_m;
    if (sector_duration_s <= 0 || m <= 0.0) return "";
    char ab[32];
    snprintf(ab, sizeof(ab), "Average %.2f", m / 1000.0 / (sector_duration_s / 3600.0) * (mph ? 0.621371 : 1.0));
    return ab;
}

std::string startDistanceText(long start_distance_m, int unit) {
    if (start_distance_m <= 0) return "";
    return formatDistance(static_cast<double>(start_distance_m), unit) + " " + distUnitName(unit);
}

long parseTimeOfDay(const std::string& text) {
    int h = 0, m = 0, s = 0, used = 0;
    int n = sscanf(text.c_str(), "%d:%d:%d%n", &h, &m, &s, &used);
    if (n == 3 && static_cast<size_t>(used) == text.size()) {
        // h:m:s
    } else {
        s = 0;
        n = sscanf(text.c_str(), "%d:%d%n", &h, &m, &used);
        if (n != 2 || static_cast<size_t>(used) != text.size()) return -1;
    }
    if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) return -1;
    return h * 3600L + m * 60L + s;
}

double parseDistanceExact(const std::string& text, int unit) {
    if (unit == DIST_M) return static_cast<double>(parseMetres(text));
    std::string tok;
    for (char c : text) {
        if (c == ' ' || c == 'k' || c == 'K' || c == 'm' || c == 'M' || c == 'i' || c == 'I') continue;
        tok += (c == ',') ? '.' : c;
    }
    if (tok.empty()) return -1;
    char* stop = nullptr;
    const double v = std::strtod(tok.c_str(), &stop);
    if (!stop || *stop != '\0' || v < 0) return -1;
    if (distIsMiles(unit)) return std::round(v * METRES_PER_MILE * 1000.0) / 1000.0;   // to the millimetre
    return static_cast<double>(std::lround(v * 1000.0));                              // km: whole metres
}

bool distanceFieldUntouched(const std::string& text, double stored_m, int unit) {
    auto plain = [](std::string t) {
        t.erase(std::remove_if(t.begin(), t.end(), [](char c) { return c == ',' || c == ' ' || c == 'k' || c == 'm' || c == 'i'; }), t.end());
        return t;
    };
    return stored_m >= 0.0 && plain(text) == plain(formatDistance(stored_m, unit));
}

long parseDistance(const std::string& text, int unit) {
    const double m = parseDistanceExact(text, unit);
    return m < 0 ? -1 : static_cast<long>(std::lround(m));
}

long parseMetres(const std::string& text) {
    std::string tok;
    for (char c : text) {
        if (c == ',' || c == ' ') continue;
        if (c == 'm' || c == 'M') continue;
        tok += c;
    }
    if (tok.empty()) return -1;
    char* stop = nullptr;
    double v = std::strtod(tok.c_str(), &stop);
    if (!stop || *stop != '\0' || v < 0) return -1;
    return static_cast<long>(std::lround(v));
}
