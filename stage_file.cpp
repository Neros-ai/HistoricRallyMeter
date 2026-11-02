#include "stage_file.h"
#include "calculations.h"
#include "sector.h"
#include "stage_rows.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <sstream>

std::string jsonEscape(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o.push_back('\\'); o.push_back(static_cast<char>(c)); }
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else if (c == '\t') o += "\\t";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o.push_back(static_cast<char>(c));
    }
    return o;
}

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}

std::vector<std::string> splitDelim(const std::string& line, char delim) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : line) {
        if (c == delim) { out.push_back(trim(cur)); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(trim(cur));
    return out;
}

bool parseNumber(const std::string& field, bool commaDecimal, double* out) {
    if (field.empty() || field == "?") return false;
    std::string t = field;
    if (commaDecimal) {
        for (char& c : t) if (c == ',') c = '.';
    }
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (end == t.c_str() || *end != '\0' || !std::isfinite(v)) return false;
    *out = v;
    return true;
}

long parseClock(const std::string& text) {
    int h = 0, m = 0, s = 0;
    if (std::sscanf(text.c_str(), "%d:%d:%d", &h, &m, &s) < 2) return -1;
    if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59) return -1;
    return h * 3600L + m * 60L + s;
}

int typeFromLabel(const std::string& name) {
    const std::string n = lower(name);
    if (n == "navi") return SEG_NAVI;
    if (n == "pc") return SEG_PC;
    if (n == "ct") return SEG_CT;
    if (n == "pm") return SEG_PM;
    if (n == "co") return SEG_CO;
    return -1;
}

const char* modeName(int mode) {
    if (mode == 2) return "italian";
    if (mode == 1) return "surrey";
    if (mode == 0) return "greek";
    return "";
}

struct RawStage {
    int stage = 0;
    bool stageSet = false;
    std::string mode, units, from, start, startDist, duration;
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
    std::vector<std::string> errors;
};

int columnIndex(const std::vector<std::string>& cols, const char* name) {
    for (size_t i = 0; i < cols.size(); i++) if (cols[i] == name) return static_cast<int>(i);
    return -1;
}

std::string fieldAt(const std::vector<std::string>& row, int idx) {
    if (idx < 0 || idx >= static_cast<int>(row.size())) return "";
    return row[idx];
}

StageFileStage buildStage(const RawStage& raw, bool commaDecimal, const std::vector<int>& earlier) {
    StageFileStage st;
    st.errors = raw.errors;
    st.stage = raw.stage;
    if (!raw.stageSet || raw.stage < 1 || raw.stage > 99)
        st.errors.push_back("stage number must be 1 to 99");
    for (int n : earlier)
        if (n == st.stage && st.stage >= 1)
            st.errors.push_back("stage " + std::to_string(st.stage) + " is in the file twice");

    const std::string mode = lower(raw.mode);
    if (mode == "greek") st.mode = 0;
    else if (mode == "surrey") st.mode = 1;
    else if (mode == "italian") st.mode = 2;
    else st.errors.push_back(raw.mode.empty() ? "mode is missing" : "unknown mode");

    const bool km = lower(raw.units) == "km";
    if (lower(raw.units) != "m" && !km) st.errors.push_back("units are missing");

    if (!raw.from.empty()) {
        double v = 0;
        if (!parseNumber(raw.from, commaDecimal, &v)) st.errors.push_back("unreadable from");
        else st.slot.first_from_m = km ? std::lround(v * 1000.0) : std::lround(v);
    }
    if (!raw.start.empty()) {
        const long clk = parseClock(raw.start);
        if (clk < 0) st.errors.push_back("unreadable start");
        else st.slot.start_time_s = clk;
    } else {
        st.warnings.push_back("no start time");
    }
    if (!raw.startDist.empty()) {
        double v = 0;
        if (!parseNumber(raw.startDist, commaDecimal, &v)) st.errors.push_back("unreadable start distance");
        else st.slot.start_distance_m = std::lround(v);
    }
    if (!raw.duration.empty()) {
        const long d = parseDurationSeconds(raw.duration);
        if (d < 0) st.errors.push_back("unreadable duration");
        else st.slot.sector_duration_s = d;
    }

    if (raw.columns.empty()) st.errors.push_back("no column line");
    for (const auto& c : raw.columns) {
        if (c != "box" && c != "type" && c != "ref" && c != "to" && c != "kph" && c != "time" && c != "auto")
            st.errors.push_back("unknown column " + c);
    }
    if (columnIndex(raw.columns, "to") < 0 && !raw.columns.empty())
        st.errors.push_back("no column line");
    if (raw.rows.empty() && !raw.columns.empty()) st.errors.push_back("no rows");

    const int iBox = columnIndex(raw.columns, "box");
    const int iType = columnIndex(raw.columns, "type");
    const int iRef = columnIndex(raw.columns, "ref");
    const int iTo = columnIndex(raw.columns, "to");
    const int iKph = columnIndex(raw.columns, "kph");
    const int iTime = columnIndex(raw.columns, "time");
    const int iAuto = columnIndex(raw.columns, "auto");

    double prevTo = st.slot.first_from_m;
    bool firstBox = true;
    for (size_t r = 0; r < raw.rows.size(); r++) {
        const auto& row = raw.rows[r];
        const std::string where = "row " + std::to_string(r + 1);
        if (row.size() != raw.columns.size()) {
            st.errors.push_back(where + ": " + std::to_string(row.size()) + " fields where the column line has "
                                + std::to_string(raw.columns.size()) + " (a comma inside a number?)");
            continue;
        }
        const std::string toText = fieldAt(row, iTo);
        double toVal = 0;
        if (!parseNumber(toText, commaDecimal, &toVal)) {
            st.errors.push_back(where + ": unreadable (" + toText + ")");
            continue;
        }
        const double toM = km ? std::lround(toVal * 1000.0) : std::lround(toVal);
        if (!(toM > prevTo)) {
            st.errors.push_back(where + ": distance does not rise");
            continue;
        }

        Segment seg;
        seg.distance_m = toM - prevTo;
        seg.autoNext = false;
        seg.changesSpeed = true;

        if (st.mode == 2) {
            const int ty = typeFromLabel(fieldAt(row, iType));
            if (ty < 0) st.errors.push_back(where + ": unknown type");
            else seg.type = ty;
            const std::string refText = fieldAt(row, iRef);
            if (!refText.empty()) {
                int ref = 0;
                if (std::sscanf(refText.c_str(), "%d", &ref) != 1) st.errors.push_back(where + ": unreadable ref");
                else seg.ref = ref;
            } else if (ty > 0) {
                seg.ref = (ty == SEG_NAVI) ? 0 : nextRef(st.slot.segments, ty);
            }
            const std::string timeText = fieldAt(row, iTime);
            const long dur = timeText.empty() ? -1 : parseDurationSeconds(timeText);
            if (!timeText.empty() && dur < 0) st.errors.push_back(where + ": unreadable duration");
            if (ty == SEG_PC && dur <= 0) st.errors.push_back(where + ": pc needs a duration");
            if (dur > 0) seg.pc_duration_s = static_cast<double>(dur);
            double kph = 0;
            const std::string kphText = fieldAt(row, iKph);
            const bool haveKph = !kphText.empty() && parseNumber(kphText, commaDecimal, &kph);
            if (!kphText.empty() && !haveKph) st.errors.push_back(where + ": unreadable (" + kphText + ")");
            if (ty == SEG_PM && !haveKph && dur <= 0) st.errors.push_back(where + ": pm needs a speed or a duration");
            if (ty == SEG_PM && haveKph) seg.target_speed_kph = kph;
            if (ty == SEG_PM && !haveKph && dur > 0 && seg.distance_m > 0)
                seg.target_speed_kph = seg.distance_m / static_cast<double>(dur) * 3.6;
            if ((ty == SEG_NAVI || ty == SEG_CT || ty == SEG_CO) && !haveKph && st.slot.sector_duration_s <= 0)
                st.errors.push_back(where + ": needs a speed");
            if (haveKph && ty != SEG_PM) seg.target_speed_kph = kph;
            if (iBox >= 0 && !fieldAt(row, iBox).empty()) st.warnings.push_back(where + ": box is ignored");
            if (iAuto >= 0 && !fieldAt(row, iAuto).empty()) st.warnings.push_back(where + ": auto is ignored");
        } else if (st.mode == 0 || st.mode == 1) {
            const std::string kphText = fieldAt(row, iKph);
            if (kphText.empty()) {
                if (r == 0) st.errors.push_back(where + ": needs a speed");
                else seg.changesSpeed = false;
            } else {
                double kph = 0;
                if (!parseNumber(kphText, commaDecimal, &kph))
                    st.errors.push_back(where + ": unreadable (" + kphText + ")");
                else seg.target_speed_kph = kph;
            }
            const std::string boxText = fieldAt(row, iBox);
            if (!boxText.empty()) {
                double box = 0;
                if (!parseNumber(boxText, false, &box)) st.errors.push_back(where + ": unreadable box");
                else {
                    seg.box = std::lround(box);
                    if (firstBox) { st.slot.first_number = seg.box; firstBox = false; }
                }
            } else if (firstBox) {
                firstBox = false;
            }
            const std::string autoText = lower(fieldAt(row, iAuto));
            if (r == 0 && !autoText.empty()) st.warnings.push_back(where + ": auto is ignored");
            else if (autoText == "y") seg.autoNext = true;
            else if (!autoText.empty() && autoText != "n") st.warnings.push_back(where + ": auto is ignored");
            if (st.mode == 1 && r > 0 && seg.changesSpeed && autoText != "y")
                st.warnings.push_back(where + ": surrey speed change is not automatic");
            if (iType >= 0 && !fieldAt(row, iType).empty()) st.warnings.push_back(where + ": type is ignored");
            if (iRef >= 0 && !fieldAt(row, iRef).empty()) st.warnings.push_back(where + ": ref is ignored");
            if (iTime >= 0 && !fieldAt(row, iTime).empty()) st.warnings.push_back(where + ": time is ignored");
        }
        st.slot.segments.push_back(seg);
        prevTo = toM;
    }

    if (st.mode == 0 || st.mode == 1) carrySpeeds(st.slot.segments);
    if (st.mode == 2 && st.errors.empty()) applySectorRules(st.slot.segments, st.slot.sector_duration_s);
    st.slot.beep_waypoints_m = deriveWaypoints(st.slot.segments);
    st.slot.waypoints_recorded = true;
    return st;
}

}  // namespace

StageFile parseStageFile(const std::string& textIn) {
    StageFile file;
    std::string text = textIn;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        text.erase(0, 3);
    if (text.size() > 65536) {
        file.errors.push_back("file is too big");
        return file;
    }

    bool semi = false;
    {
        std::istringstream scan(text);
        std::string line;
        while (std::getline(scan, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const std::string t = trim(line);
            if (t.empty() || t[0] == '#') continue;
            if (t.find(';') != std::string::npos) { semi = true; break; }
        }
    }
    const char delim = semi ? ';' : ',';

    std::vector<RawStage> raws;
    RawStage cur;
    bool open = false;
    bool any = false;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#') continue;
        any = true;
        auto fields = splitDelim(t, delim);
        const bool header = cur.columns.empty();
        bool isColumn = false;
        if (header) {
            for (const auto& f : fields) if (lower(f) == "to") isColumn = true;
        }
        if (header && !isColumn) {
            if (fields.empty()) continue;
            const std::string key = lower(fields[0]);
            const std::string val = fields.size() >= 2 ? fields[1] : "";
            if (key == "stage") {
                if (open) raws.push_back(cur);
                cur = RawStage();
                open = true;
                int n = 0;
                if (std::sscanf(val.c_str(), "%d", &n) == 1) { cur.stage = n; cur.stageSet = true; }
                continue;
            }
            if (!open) {
                file.errors.push_back("nothing to read");
                return file;
            }
            // "start distance" arrives as two fields when the separator is a comma
            // between the words. Join a following "distance" back onto "start".
            if (key == "start distance") cur.startDist = val;
            else if (key == "start") cur.start = val;
            else if (key == "mode") cur.mode = val;
            else if (key == "units") cur.units = val;
            else if (key == "from") cur.from = val;
            else if (key == "duration") cur.duration = val;
        } else if (header && isColumn) {
            if (!open) {
                file.errors.push_back("nothing to read");
                return file;
            }
            for (const auto& f : fields) cur.columns.push_back(lower(f));
        } else {
            cur.rows.push_back(fields);
        }
    }
    if (open) raws.push_back(cur);
    if (!any) file.errors.push_back("nothing to read");

    std::vector<int> earlier;
    for (const RawStage& raw : raws) {
        file.stages.push_back(buildStage(raw, semi, earlier));
        if (file.stages.back().stage >= 1) earlier.push_back(file.stages.back().stage);
    }
    return file;
}

bool stageFileFitsMode(int mode, int rally_type) {
    if (mode == 2) return rally_type == 2;
    if (mode == 0 || mode == 1) return rally_type != 2;
    return false;
}

std::string uploadRefusal(const RallyState& s, const StageFile& f) {
    if (stageLive(s)) return "a stage is running: upload after it ends";
    if (!f.errors.empty()) return f.errors.front();
    for (const auto& st : f.stages) {
        if (st.mode >= 0 && !stageFileFitsMode(st.mode, s.rally_type)) {
            if (s.rally_type == 2) return "this file is not italian";
            return "this file is italian and the box is not";
        }
    }
    bool any = false;
    for (const auto& st : f.stages) if (st.loadable()) any = true;
    if (!any) {
        for (const auto& st : f.stages)
            if (!st.errors.empty()) return st.errors.front();
        return "nothing to load";
    }
    return "";
}

std::vector<int> uploadOverwrites(const RallyState& s, const StageFile& f) {
    std::vector<int> out;
    for (const auto& st : f.stages) {
        if (!st.loadable() || st.stage < 1 || st.stage > RallyState::MAX_MEMORY_SLOTS) continue;
        if (!s.memory_slots[st.stage - 1].empty()) out.push_back(st.stage);
    }
    return out;
}

std::string overwriteQuestion(const std::vector<int>& stages) {
    if (stages.empty()) return "";
    if (stages.size() == 1)
        return "stage " + std::to_string(stages[0]) + " is already programmed — replace?";
    std::string list;
    for (size_t i = 0; i < stages.size(); i++) {
        if (i == 0) list = std::to_string(stages[i]);
        else if (i + 1 == stages.size()) list += " and " + std::to_string(stages[i]);
        else list += ", " + std::to_string(stages[i]);
    }
    return "stages " + list + " are already programmed — replace?";
}

std::vector<int> applyStageFile(RallyState& s, const StageFile& f) {
    std::vector<int> written;
    for (const auto& st : f.stages) {
        if (!st.loadable() || st.stage < 1 || st.stage > RallyState::MAX_MEMORY_SLOTS) continue;
        RallyState::MemorySlot& slot = s.memory_slots[st.stage - 1];
        slot.clear();
        slot.segments = st.slot.segments;
        slot.beep_waypoints_m = st.slot.beep_waypoints_m;
        slot.waypoints_recorded = true;
        slot.start_time_s = st.slot.start_time_s;
        slot.start_distance_m = st.slot.start_distance_m;
        slot.first_number = st.slot.first_number;
        slot.first_from_m = st.slot.first_from_m;
        slot.sector_duration_s = st.slot.sector_duration_s;
        recomputeSegmentCounts(slot.segments, s.calibration);
        if (s.last_memory_slot == st.stage) s.segments = slot.segments;
        written.push_back(st.stage);
    }
    return written;
}

std::string uploadStateJson(const std::string& id, const RallyState& s, const StageFile& f,
                            const std::vector<int>& loaded) {
    const std::string refusal = id.empty() ? "" : uploadRefusal(s, f);
    const auto over = id.empty() ? std::vector<int>() : uploadOverwrites(s, f);
    std::ostringstream o;
    o << std::fixed;
    o << "{\"id\":\"" << jsonEscape(id) << "\",\"refusal\":\"" << jsonEscape(refusal)
      << "\",\"question\":\"" << jsonEscape(refusal.empty() ? overwriteQuestion(over) : "")
      << "\",\"stages\":[";
    bool first = true;
    if (!id.empty()) {
        for (const auto& st : f.stages) {
            if (!first) o << ',';
            first = false;
            const auto rows = buildStageRows(st.slot.segments, st.slot.first_number, st.slot.first_from_m);
            const double dur = rows.empty() ? 0 : rows.back().total_s;
            const bool replaces = st.stage >= 1 && st.stage <= RallyState::MAX_MEMORY_SLOTS &&
                                  !s.memory_slots[st.stage - 1].empty();
            double from = st.slot.first_from_m;
            double to = from;
            for (const Segment& seg : st.slot.segments) to += seg.distance_m;
            o << "{\"stage\":" << st.stage << ",\"mode\":\"" << modeName(st.mode) << "\",\"rows\":"
              << st.slot.segments.size() << ",\"first\":" << st.slot.first_number
              << ",\"from_m\":" << std::lround(from) << ",\"to_m\":" << std::lround(to)
              << ",\"duration_s\":" << std::lround(st.slot.sector_duration_s > 0 ? st.slot.sector_duration_s : dur)
              << ",\"start_s\":" << st.slot.start_time_s
              << ",\"replaces\":" << (replaces ? "true" : "false") << ",\"errors\":[";
            for (size_t i = 0; i < st.errors.size(); i++) {
                if (i) o << ',';
                o << '"' << jsonEscape(st.errors[i]) << '"';
            }
            o << "],\"warnings\":[";
            for (size_t i = 0; i < st.warnings.size(); i++) {
                if (i) o << ',';
                o << '"' << jsonEscape(st.warnings[i]) << '"';
            }
            o << "],\"table\":[";
            for (size_t i = 0; i < rows.size() && i < st.slot.segments.size(); i++) {
                if (i) o << ',';
                const StageRow& row = rows[i];
                const Segment& seg = st.slot.segments[i];
                o << "{\"box\":" << row.number << ",\"type\":\"" << typeLabel(seg.type) << "\",\"ref\":" << seg.ref
                  << ",\"from_m\":" << std::lround(row.from_m) << ",\"to_m\":" << std::lround(row.to_m)
                  << ",\"kph\":" << row.kph << ",\"dur_s\":" << std::lround(row.time_s)
                  << ",\"elapsed_s\":" << std::lround(row.total_s) << "}";
            }
            o << "]}";
        }
    }
    o << "],\"loaded\":[";
    for (size_t i = 0; i < loaded.size(); i++) { if (i) o << ','; o << loaded[i]; }
    o << "]}";
    return o.str();
}

bool jsonUnescape(const std::string& raw, std::string* out) {
    std::string s;
    for (size_t i = 0; i < raw.size(); i++) {
        if (raw[i] != '\\') { s.push_back(raw[i]); continue; }
        if (i + 1 >= raw.size()) return false;
        const char n = raw[++i];
        if (n == '"' || n == '\\' || n == '/') s.push_back(n);
        else if (n == 'n') s.push_back('\n');
        else if (n == 'r') s.push_back('\r');
        else if (n == 't') s.push_back('\t');
        else if (n == 'b') s.push_back('\b');
        else if (n == 'f') s.push_back('\f');
        else if (n == 'u' && i + 4 < raw.size()) {
            int cp = 0;
            for (int k = 0; k < 4; k++) {
                const char h = raw[++i];
                cp <<= 4;
                if (h >= '0' && h <= '9') cp += h - '0';
                else if (h >= 'a' && h <= 'f') cp += h - 'a' + 10;
                else if (h >= 'A' && h <= 'F') cp += h - 'A' + 10;
                else return false;
            }
            if (cp < 0x80) s.push_back(static_cast<char>(cp));
            else if (cp < 0x800) {
                s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
                s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
        } else return false;
    }
    if (s.size() > 65536) return false;
    *out = s;
    return true;
}

bool jsonFindEscapedString(const char* json, const char* key, std::string* out) {
    const std::string needle = std::string("\"") + key + "\":\"";
    const char* p = strstr(json, needle.c_str());
    if (!p) return false;
    p += needle.size();
    std::string raw;
    while (*p) {
        if (*p == '\\') {
            if (!p[1]) return false;
            raw.push_back(*p++);
            raw.push_back(*p++);
            continue;
        }
        if (*p == '"') return jsonUnescape(raw, out);
        raw.push_back(*p++);
    }
    return false;
}

#ifndef RALLY_NO_GTK
#include "stage_edit.h"

std::vector<int> stageUploadCommit(AppData* data, const StageFile& f) {
    std::vector<int> written = applyStageFile(*data->state, f);
    if (!written.empty()) {
        stageEditSelect(data, written.front());
        stageEditCommit(data);
    }
    return written;
}
#endif
