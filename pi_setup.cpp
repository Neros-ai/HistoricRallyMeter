#include "pi_setup.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <sstream>
#include <unistd.h>
#include <sys/wait.h>

static std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) i++;
    return s.substr(i);
}

static bool swapsAxes(const std::string& t) {
    return t == "90" || t == "270" || t == "flipped-90" || t == "flipped-270";
}

int PiOutput::logicalW() const { return swapsAxes(transform) ? mode_h : mode_w; }
int PiOutput::logicalH() const { return swapsAxes(transform) ? mode_w : mode_h; }

std::string PiOutput::orientationName() const {
    if (transform == "normal") return "normal";
    if (transform == "90") return "left";
    if (transform == "180") return "inverted";
    if (transform == "270") return "right";
    if (transform == "flipped") return "flipped";
    if (transform == "flipped-90") return "flipped-left";
    if (transform == "flipped-180") return "flipped-inverted";
    if (transform == "flipped-270") return "flipped-right";
    return transform;
}

std::string cycleTransform(const std::string& current) {
    static const char* order[] = {"normal", "90", "180", "270"};
    for (int i = 0; i < 4; i++) {
        if (current == order[i]) return order[(i + 1) % 4];
    }
    return "normal";
}

static bool knownTransform(const std::string& t) {
    return t == "normal" || t == "90" || t == "180" || t == "270" ||
           t == "flipped" || t == "flipped-90" || t == "flipped-180" || t == "flipped-270";
}

static bool safeToken(const std::string& s) {
    if (s.empty() || s.size() > 64) return false;
    for (char c : s) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '.')
            return false;
    }
    return true;
}

static std::string shQuote(const std::string& s);

static int runShell(const std::string& script, std::string& output) {
    std::string quoted = "'";
    for (char c : script) {
        if (c == '\'') quoted += "'\\''";
        else quoted += c;
    }
    quoted += "'";
    std::string cmd = "bash -c " + quoted + " 2>&1";
    output.clear();
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) {
        output = "could not run command";
        return -1;
    }
    char buf[512];
    while (fgets(buf, sizeof(buf), fp)) output += buf;
    int status = pclose(fp);
    if (status == -1) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return -1;
}

static std::string readTextFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<PiOutput> readPiOutputs() {
    std::string text;
    if (runShell("wlr-randr", text) != 0 && text.empty()) return {};
    std::vector<PiOutput> outs;
    PiOutput cur;
    bool in = false;
    bool enabled = true;
    std::istringstream lines(text);
    std::string line;
    auto finish = [&]() {
        if (in && enabled && !cur.name.empty() && cur.mode_w > 0) outs.push_back(cur);
        cur = PiOutput();
        in = false;
        enabled = true;
    };
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line[0] != ' ' && line[0] != '\t') {
            finish();
            std::string name = line.substr(0, line.find(' '));
            if (!safeToken(name)) continue;
            cur.name = name;
            in = true;
            continue;
        }
        if (!in) continue;
        std::string t = trim(line);
        if (t.rfind("Enabled:", 0) == 0) {
            enabled = t.find("yes") != std::string::npos;
        } else if (t.find(" px,") != std::string::npos && t.find(" Hz") != std::string::npos) {
            PiMode m;
            if (sscanf(t.c_str(), "%dx%d px, %lf Hz", &m.w, &m.h, &m.refresh) == 3) {
                m.preferred = t.find("preferred") != std::string::npos;
                m.current = t.find("current") != std::string::npos;
                cur.modes.push_back(m);
                if (m.current) {
                    cur.mode_w = m.w;
                    cur.mode_h = m.h;
                    cur.refresh = m.refresh;
                }
            }
        } else if (t.rfind("Position:", 0) == 0) {
            sscanf(t.c_str(), "Position: %d,%d", &cur.x, &cur.y);
        } else if (t.rfind("Transform:", 0) == 0) {
            std::string tr = trim(t.substr(std::strlen("Transform:")));
            if (knownTransform(tr)) cur.transform = tr;
        } else if (t.rfind("Scale:", 0) == 0) {
            sscanf(t.c_str(), "Scale: %lf", &cur.scale);
            if (cur.scale <= 0) cur.scale = 1;
        }
    }
    finish();
    return outs;
}

static std::string modeHz(const PiOutput& o) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%dx%d@%.3fHz", o.mode_w, o.mode_h, o.refresh);
    return buf;
}

static std::string modeKanshi(const PiOutput& o) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%dx%d@%.3f", o.mode_w, o.mode_h, o.refresh);
    return buf;
}

static bool applyRandr(const std::vector<PiOutput>& outputs, std::string& error) {
    if (outputs.empty()) {
        error = "No displays found";
        return false;
    }
    std::string script = "wlr-randr";
    for (const auto& o : outputs) {
        if (!safeToken(o.name) || !knownTransform(o.transform) || o.mode_w <= 0 || o.mode_h <= 0) {
            error = "Display '" + o.name + "' has no usable mode";
            return false;
        }
        char pos[64];
        std::snprintf(pos, sizeof(pos), "%d,%d", o.x, o.y);
        char scale[32];
        std::snprintf(scale, sizeof(scale), "%f", o.scale);
        script += " --output " + o.name + " --on --mode " + modeHz(o) +
                  " --pos " + pos + " --transform " + o.transform + " --scale " + scale;
    }
    std::string out;
    int rc = runShell(script, out);
    if (rc != 0) {
        error = trim(out);
        if (error.empty()) error = "wlr-randr failed";
        return false;
    }
    return true;
}

static std::string kanshiPath() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) home = "/home/simonpapworth6742";
    return std::string(home) + "/.config/kanshi/config";
}

static std::set<std::string> outputNamesIn(const std::string& block) {
    std::set<std::string> names;
    std::istringstream lines(block);
    std::string line;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.rfind("output ", 0) != 0) continue;
        std::istringstream ls(line);
        std::string word, name;
        ls >> word >> name;
        if (safeToken(name)) names.insert(name);
    }
    return names;
}

static bool saveKanshi(const std::vector<PiOutput>& outputs, std::string& error) {
    std::set<std::string> want;
    for (const auto& o : outputs) want.insert(o.name);

    std::ostringstream body;
    body << "profile {\n";
    for (const auto& o : outputs) {
        char scale[32];
        std::snprintf(scale, sizeof(scale), "%.6f", o.scale);
        body << "\t\toutput " << o.name << " enable scale " << scale
             << " mode " << modeKanshi(o)
             << " position " << o.x << "," << o.y
             << " transform " << o.transform << "\n";
    }
    body << "}\n";
    std::string fresh = body.str();

    std::string path = kanshiPath();
    std::string existing = readTextFile(path);
    std::vector<std::string> others;
    std::string leading;
    size_t i = 0;
    bool first = true;
    while (i < existing.size()) {
        size_t p = existing.find("profile", i);
        if (p == std::string::npos) break;
        if (first) {
            leading = existing.substr(0, p);
            first = false;
        }
        size_t brace = existing.find('{', p);
        if (brace == std::string::npos) break;
        int depth = 0;
        size_t j = brace;
        for (; j < existing.size(); j++) {
            if (existing[j] == '{') depth++;
            else if (existing[j] == '}') {
                depth--;
                if (depth == 0) { j++; break; }
            }
        }
        if (j >= existing.size() && depth != 0) break;
        std::string block = existing.substr(p, j - p);
        if (outputNamesIn(block) != want) others.push_back(block);
        i = j;
    }

    std::string dir = path.substr(0, path.rfind('/'));
    std::string mk;
    runShell("mkdir -p " + shQuote(dir), mk);

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        error = "Could not write " + path;
        return false;
    }
    if (!trim(leading).empty()) out << leading;
    out << fresh << "\n";
    for (const auto& block : others) out << block << "\n\n";
    out.close();

    std::string ping;
    runShell("pidof kanshi >/dev/null 2>&1 && kill -HUP $(pidof kanshi) || true", ping);
    return true;
}

bool applyPiLayout(const std::vector<PiOutput>& outputs, std::string& error) {
    if (!applyRandr(outputs, error)) return false;
    if (!saveKanshi(outputs, error)) return false;
    return true;
}

bool rotatePiOutput(const std::string& name, std::string& error) {
    auto outputs = readPiOutputs();
    bool found = false;
    for (auto& o : outputs) {
        if (o.name == name) {
            o.transform = cycleTransform(o.transform);
            found = true;
            break;
        }
    }
    if (!found) {
        error = "Display " + name + " is not attached";
        return false;
    }
    return applyPiLayout(outputs, error);
}

static const PiMode* findMode(const PiOutput& o, int w, int h) {
    const PiMode* best = nullptr;
    for (const auto& m : o.modes) {
        if (m.w != w || m.h != h) continue;
        if (!best || m.preferred || (m.current && !best->preferred)) best = &m;
    }
    return best;
}

static void useMode(PiOutput& o, const PiMode& m) {
    o.mode_w = m.w;
    o.mode_h = m.h;
    o.refresh = m.refresh;
}

bool resetPiLayout(std::string& error) {
    auto outputs = readPiOutputs();
    if (outputs.empty()) {
        error = "No displays found";
        return false;
    }
    int copilot = -1;
    int driver = -1;
    for (int i = 0; i < static_cast<int>(outputs.size()); i++) {
        if (copilot < 0 && (findMode(outputs[i], 400, 1280) || findMode(outputs[i], 1280, 400)))
            copilot = i;
    }
    for (int i = 0; i < static_cast<int>(outputs.size()); i++) {
        if (i == copilot) continue;
        if (driver < 0 && (findMode(outputs[i], 800, 480) || findMode(outputs[i], 480, 800)))
            driver = i;
    }
    if (copilot >= 0) {
        if (const PiMode* m = findMode(outputs[copilot], 400, 1280)) {
            useMode(outputs[copilot], *m);
            outputs[copilot].transform = "90";
        } else if (const PiMode* m = findMode(outputs[copilot], 1280, 400)) {
            useMode(outputs[copilot], *m);
            outputs[copilot].transform = "normal";
        }
    }
    if (driver >= 0) {
        if (const PiMode* m = findMode(outputs[driver], 800, 480)) {
            useMode(outputs[driver], *m);
            outputs[driver].transform = "normal";
        } else if (const PiMode* m = findMode(outputs[driver], 480, 800)) {
            useMode(outputs[driver], *m);
            outputs[driver].transform = "normal";
        }
    }

    std::vector<int> order;
    if (copilot >= 0) order.push_back(copilot);
    if (driver >= 0) order.push_back(driver);
    std::vector<int> rest;
    for (int i = 0; i < static_cast<int>(outputs.size()); i++) {
        if (i != copilot && i != driver) rest.push_back(i);
    }
    std::sort(rest.begin(), rest.end(), [&](int a, int b) {
        if (outputs[a].x != outputs[b].x) return outputs[a].x < outputs[b].x;
        return outputs[a].name < outputs[b].name;
    });
    for (int idx : rest) order.push_back(idx);

    int x = 0;
    for (int idx : order) {
        outputs[idx].x = x;
        outputs[idx].y = 0;
        x += std::max(1, outputs[idx].logicalW());
    }
    return applyPiLayout(outputs, error);
}

std::string piHostname() {
    char buf[256];
    if (gethostname(buf, sizeof(buf)) != 0) return "raspberrypi";
    buf[sizeof(buf) - 1] = '\0';
    std::string h = buf;
    auto dot = h.find('.');
    if (dot != std::string::npos) h = h.substr(0, dot);
    if (!safeToken(h)) return "raspberrypi";
    return h;
}

std::string wifiClientSsid() {
    return "WiFi4" + piHostname();
}

static std::string shQuote(const std::string& s) {
    std::string o = "'";
    for (char c : s) {
        if (c == '\'') o += "'\\''";
        else o += c;
    }
    o += "'";
    return o;
}

static void disableOtherWifi(const std::string& keep, std::string& script) {
    script +=
        "nmcli -t -f NAME,TYPE connection show | while IFS= read -r line; do "
        "name=${line%%:*}; type=${line#*:}; "
        "case \"$type\" in *wireless*) "
        "[ \"$name\" = " + shQuote(keep) + " ] || "
        "nmcli connection modify \"$name\" connection.autoconnect no ;; "
        "esac; done\n";
}

bool startWifiHotspot(std::string& detail) {
    std::string host = piHostname();
    std::string q = shQuote(host);
    std::string script =
        "nmcli radio wifi on\n"
        "if nmcli -t -f NAME connection show | grep -qx hrm-hotspot; then\n"
        "  nmcli connection modify hrm-hotspot "
        "802-11-wireless.ssid " + q + " "
        "802-11-wireless.mode ap 802-11-wireless.band bg "
        "ipv4.method shared "
        "connection.autoconnect yes connection.autoconnect-priority 100 "
        "connection.interface-name wlan0\n"
        "  nmcli connection modify hrm-hotspot remove 802-11-wireless-security >/dev/null 2>&1 || true\n"
        "else\n"
        "  nmcli connection add type wifi ifname wlan0 con-name hrm-hotspot autoconnect yes "
        "ssid " + q + " "
        "802-11-wireless.mode ap 802-11-wireless.band bg "
        "ipv4.method shared\n"
        "  nmcli connection modify hrm-hotspot connection.autoconnect-priority 100\n"
        "fi\n";
    disableOtherWifi("hrm-hotspot", script);
    script += "nmcli -w 25 connection up hrm-hotspot\n";
    script = "set -e\n" + script;
    int rc = runShell(script, detail);
    detail = trim(detail);
    if (rc != 0) {
        if (detail.empty()) detail = "Could not start the hotspot";
        return false;
    }
    if (detail.empty()) detail = "Hotspot " + host + " is on";
    return true;
}

bool joinWifiClient(std::string& detail) {
    std::string ssid = wifiClientSsid();
    std::string q = shQuote(ssid);
    std::string script =
        "set -e\n"
        "nmcli radio wifi on\n"
        "nmcli connection modify hrm-hotspot connection.autoconnect no >/dev/null 2>&1 || true\n"
        "nmcli connection down hrm-hotspot >/dev/null 2>&1 || true\n"
        "nmcli -w 30 device wifi connect " + q + " ifname wlan0\n";
    disableOtherWifi(ssid, script);
    script += "nmcli connection modify " + q +
              " connection.autoconnect yes connection.autoconnect-priority 100 || true\n";
    int rc = runShell(script, detail);
    detail = trim(detail);
    if (rc != 0) {
        if (detail.empty()) detail = "Could not join " + ssid;
        return false;
    }
    if (detail.empty()) detail = "Joined " + ssid;
    return true;
}

std::vector<SavedWifi> savedWifiNetworks() {
    std::vector<SavedWifi> out;
    std::string list;
    if (runShell("nmcli -t -e no -f NAME,TYPE,ACTIVE connection show", list) != 0) return out;
    std::istringstream lines(list);
    std::string line;
    while (std::getline(lines, line)) {
        // NAME may itself hold a colon; TYPE and ACTIVE never do, so split from the right.
        size_t c2 = line.rfind(':');
        if (c2 == std::string::npos) continue;
        size_t c1 = line.rfind(':', c2 - 1);
        if (c1 == std::string::npos || c2 == 0) continue;
        const std::string type = line.substr(c1 + 1, c2 - c1 - 1);
        if (type.find("wireless") == std::string::npos) continue;
        SavedWifi w;
        w.name = line.substr(0, c1);
        w.active = trim(line.substr(c2 + 1)) == "yes";
        std::string mode;
        runShell("nmcli -g 802-11-wireless.mode connection show " + shQuote(w.name), mode);
        w.hotspot = trim(mode) == "ap";
        out.push_back(w);
    }
    std::stable_sort(out.begin(), out.end(), [](const SavedWifi& a, const SavedWifi& b) {
        return a.hotspot && !b.hotspot;
    });
    return out;
}

bool useSavedWifi(const std::string& name, std::string& detail) {
    std::string out;
    int rc = runShell("nmcli radio wifi on; nmcli -w 30 connection up " + shQuote(name), out);
    if (rc != 0) {
        detail = trim(out);
        if (detail.empty()) detail = "Could not switch to " + name;
        return false;
    }
    detail = "Now on " + name;
    return true;
}

std::string wifiStatusLine() {
    std::string out;
    runShell("nmcli -t -f NAME,TYPE,DEVICE connection show --active", out);
    std::string wifi;
    std::istringstream lines(out);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.find("wireless") == std::string::npos && line.find("wifi") == std::string::npos)
            continue;
        std::string name = line.substr(0, line.find(':'));
        if (name == "lo") continue;
        if (!wifi.empty()) wifi += ", ";
        wifi += name;
    }
    if (wifi.empty()) return "Wi-Fi is not connected";
    return "Wi-Fi: " + wifi;
}

static const char* kKeyboardOutputFile = "/usr/share/squeekboard/output";
static const char* kKeyboardAutostart = "/etc/xdg/autostart/squeekboard.desktop";
static const char* kKeyboardGreeter = "/etc/xdg/labwc-greeter/autostart";

static std::string preferredKeyboardOutput() {
    std::string text = readTextFile(kKeyboardOutputFile);
    const std::string key = "SQUEEKBOARD_PREFERRED_OUTPUT=";
    size_t pos = text.find(key);
    if (pos == std::string::npos) return {};
    std::string rest = text.substr(pos + key.size());
    size_t end = rest.find_first_of("\r\n ");
    if (end != std::string::npos) rest = rest.substr(0, end);
    return trim(rest);
}

static bool keyboardAutostartOn() {
    std::ifstream in(kKeyboardAutostart);
    return static_cast<bool>(in);
}

bool keyboardOnOutput(const std::string& output) {
    return keyboardAutostartOn() && preferredKeyboardOutput() == output;
}

static std::string waylandKeyboardLaunch(bool show) {
    const char* wl = std::getenv("WAYLAND_DISPLAY");
    if (!wl || !*wl) wl = "wayland-0";
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    std::string runtimeDir = (runtime && *runtime) ? runtime : "/run/user/" + std::to_string(getuid());
    const char* bus = std::getenv("DBUS_SESSION_BUS_ADDRESS");
    std::string script =
        "pkill -x squeekboard >/dev/null 2>&1 || true\n"
        "sleep 0.3\n";
    if (!show) return script;
    script += "env -u DISPLAY WAYLAND_DISPLAY=" + std::string(wl) +
              " GDK_BACKEND=wayland XDG_RUNTIME_DIR=" + runtimeDir;
    if (bus && *bus) script += " DBUS_SESSION_BUS_ADDRESS=" + std::string(bus);
    script +=
        " setsid /usr/bin/sbout >/tmp/squeekboard-hrm.log 2>&1 </dev/null &\n"
        "set +e\n"
        "shown=0\n"
        "for i in 1 2 3 4 5 6 7 8; do\n"
        "  if busctl --user call sm.puri.OSK0 /sm/puri/OSK0 sm.puri.OSK0 SetVisible b true; then\n"
        "    shown=1\n"
        "    break\n"
        "  fi\n"
        "  sleep 0.3\n"
        "done\n"
        "if [ \"$shown\" != 1 ]; then\n"
        "  echo 'Keyboard did not appear' >&2\n"
        "  exit 1\n"
        "fi\n";
    return script;
}

bool setOnScreenKeyboard(const std::string& output, bool enable, std::string& error) {
    if (!safeToken(output)) {
        error = "Display name cannot be used for the keyboard";
        return false;
    }
    std::string script = "set -e\n";
    if (enable) {
        script +=
            "printf '%s\\n' '#!/bin/sh' 'export SQUEEKBOARD_PREFERRED_OUTPUT=" + output +
            "' | sudo -n tee " + std::string(kKeyboardOutputFile) + " >/dev/null\n"
            "sudo -n chmod a+x " + std::string(kKeyboardOutputFile) + "\n"
            "sudo -n tee " + std::string(kKeyboardAutostart) + " >/dev/null <<EOF\n"
            "[Desktop Entry]\n"
            "Name=Squeekboard\n"
            "Comment=Launch the on-screen keyboard\n"
            "Exec=/usr/bin/sbout\n"
            "Terminal=false\n"
            "Type=Application\n"
            "NoDisplay=true\n"
            "EOF\n"
            "if [ -f " + std::string(kKeyboardGreeter) + " ]; then\n"
            "  sudo -n sed -i '\\|/usr/bin/sbtest|d' " + std::string(kKeyboardGreeter) + "\n"
            "  grep -q '/usr/bin/sbout' " + std::string(kKeyboardGreeter) +
            " || echo '/usr/bin/sbout &' | sudo -n tee -a " + std::string(kKeyboardGreeter) + " >/dev/null\n"
            "fi\n";
        script += waylandKeyboardLaunch(true);
    } else {
        script +=
            "pkill -x squeekboard >/dev/null 2>&1 || true\n"
            "sudo -n rm -f " + std::string(kKeyboardAutostart) + "\n"
            "if [ -f " + std::string(kKeyboardGreeter) + " ]; then\n"
            "  sudo -n sed -i '\\|/usr/bin/sbout|d;\\|/usr/bin/sbtest|d' " + std::string(kKeyboardGreeter) + "\n"
            "fi\n";
    }
    int rc = runShell(script, error);
    error = trim(error);
    if (rc != 0) {
        if (error.empty()) error = enable ? "Could not enable the keyboard" : "Could not disable the keyboard";
        return false;
    }
    error.clear();
    return true;
}
