#include "ui_setup.h"
#include "callbacks.h"
#include "config_file.h"
#include "nero_frame.h"
#include "pi_setup.h"
#include "rally_state.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int KEY_H = 34;
constexpr int SPANNER_W = 70, SPANNER_H = 70;

void addClass(GtkWidget* w, const char* c) { gtk_style_context_add_class(gtk_widget_get_style_context(w), c); }

GtkWidget* text(const std::string& t, const char* cls = "setup-label") {
    GtkWidget* l = gtk_label_new(t.c_str());
    addClass(l, cls);
    gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
    return l;
}

GtkWidget* key(const std::string& label) {
    GtkWidget* k = gtk_button_new_with_label(label.c_str());
    gtk_widget_set_size_request(k, -1, KEY_H);
    addClass(k, "dist-heading");
    addClass(k, "key-small");
    return k;
}

std::string readCommand(const char* cmd) {
    std::string out;
    FILE* fp = popen(cmd, "r");
    if (!fp) return out;
    char buf[256];
    while (fgets(buf, sizeof(buf), fp)) out += buf;
    pclose(fp);
    return out;
}

std::string trimCopy(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    size_t start = 0;
    while (start < s.size() && s[start] == ' ') start++;
    return s.substr(start);
}

// bluez_output.88_FA_7F_13_20_EA.1 -> 88:FA:7F:13:20:EA
bool bluetoothAddressFromSink(const std::string& sink, std::string& address) {
    const std::string prefix = "bluez_output.";
    size_t pos = sink.find(prefix);
    if (pos == std::string::npos) return false;
    std::string rest = trimCopy(sink.substr(pos + prefix.size()));
    size_t dot = rest.rfind('.');
    if (dot != std::string::npos) {
        bool digits = dot + 1 < rest.size();
        for (size_t i = dot + 1; digits && i < rest.size(); i++)
            if (!std::isdigit(static_cast<unsigned char>(rest[i]))) digits = false;
        if (digits) rest = rest.substr(0, dot);
    }
    if (rest.size() != 17) return false;
    for (size_t i = 0; i < rest.size(); i++) {
        if (i % 3 == 2) {
            if (rest[i] != '_') return false;
            rest[i] = ':';
        } else if (!std::isxdigit(static_cast<unsigned char>(rest[i]))) {
            return false;
        }
    }
    address = rest;
    return true;
}

bool bluetoothAddressOk(const std::string& address) {
    if (address.size() != 17) return false;
    for (size_t i = 0; i < address.size(); i++) {
        unsigned char c = static_cast<unsigned char>(address[i]);
        if (i % 3 == 2) {
            if (address[i] != ':') return false;
        } else if (!std::isxdigit(c)) {
            return false;
        }
    }
    return true;
}

std::string bluetoothDeviceName(const std::string& address) {
    std::string info = readCommand(("bluetoothctl info " + address + " 2>/dev/null").c_str());
    std::string alias, name;
    std::istringstream lines(info);
    std::string line;
    while (std::getline(lines, line)) {
        auto take = [&](const char* k, std::string& dest) {
            std::string prefix = std::string(k) + ":";
            size_t at = line.find(prefix);
            if (at == std::string::npos) return;
            dest = trimCopy(line.substr(at + prefix.size()));
        };
        take("Alias", alias);
        take("Name", name);
    }
    if (!alias.empty()) return alias;
    if (!name.empty()) return name;
    return address;
}

void spawnBluetoothConnect(const std::string& address, bool trust) {
    std::string underscored = address;
    for (char& c : underscored) if (c == ':') c = '_';
    std::string script;
    if (trust) script += "bluetoothctl trust '" + address + "' >/dev/null 2>&1; ";
    script +=
        "bluetoothctl connect '" + address + "' >/dev/null 2>&1; "
        "sink='bluez_output." + underscored + ".1'; "
        "for i in 1 2 3 4 5 6 7 8; do "
        "if pactl list short sinks 2>/dev/null | grep -q \"$sink\"; then "
        "pactl set-default-sink \"$sink\"; exit 0; fi; sleep 0.5; done";
    gchar* argv[] = { (gchar*)"bash", (gchar*)"-c", const_cast<gchar*>(script.c_str()), nullptr };
    g_spawn_async(nullptr, argv, nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, nullptr);
}

void pumpUi() {
    while (gtk_events_pending()) gtk_main_iteration();
}

void setStatus(AppData* d, const std::string& t) {
    if (d && d->setupStatusLabel) gtk_label_set_text(d->setupStatusLabel, t.c_str());
}

void showRememberedBluetooth(AppData* d) {
    if (!d->bluetoothAudioLabel) return;
    const std::string& shown = !d->state->bluetooth_audio_name.empty()
        ? d->state->bluetooth_audio_name : d->state->bluetooth_audio_address;
    gtk_label_set_text(d->bluetoothAudioLabel, shown.c_str());
}

void refreshAudioKeys(AppData* d) {
    const bool on = !d->state->bluetooth_audio_address.empty();
    for (GtkWidget* b : {d->setupBtConnectBtn, d->setupBtDisconnectBtn}) {
        if (!b) continue;
        if (on) gtk_widget_show(b); else gtk_widget_hide(b);
    }
}

// One card per connected display: name, size, orientation, rotate and keyboard keys.
void onRotate(GtkWidget* w, gpointer p);
void onKeyboard(GtkWidget* w, gpointer p);

void rebuildDisplays(AppData* d) {
    if (!d->setupDisplayRow) return;
    GList* children = gtk_container_get_children(GTK_CONTAINER(d->setupDisplayRow));
    for (GList* c = children; c; c = c->next) gtk_widget_destroy(GTK_WIDGET(c->data));
    g_list_free(children);

    std::vector<PiOutput> outputs = readPiOutputs();
    std::sort(outputs.begin(), outputs.end(), [](const PiOutput& a, const PiOutput& b) {
        if (a.x != b.x) return a.x < b.x;
        return a.name < b.name;
    });
    if (outputs.empty())
        gtk_box_pack_start(GTK_BOX(d->setupDisplayRow), text("No displays found"), FALSE, FALSE, 0);

    for (size_t i = 0; i < outputs.size(); i++) {
        if (i > 0) {
            const int gap = outputs[i].x - (outputs[i - 1].x + outputs[i - 1].logicalW());
            GtkWidget* g = text(std::to_string(gap) + " px");
            gtk_widget_set_valign(g, GTK_ALIGN_CENTER);
            gtk_box_pack_start(GTK_BOX(d->setupDisplayRow), g, FALSE, FALSE, 12);
        }
        const PiOutput& out = outputs[i];
        GtkWidget* card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 3);
        gtk_box_pack_start(GTK_BOX(card), text(out.name), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(card),
            text(std::to_string(out.logicalW()) + "x" + std::to_string(out.logicalH())), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(card), text(out.orientationName()), FALSE, FALSE, 0);

        GtkWidget* rotate = key("rotate");
        g_object_set_data_full(G_OBJECT(rotate), "output-name", g_strdup(out.name.c_str()), g_free);
        g_signal_connect(rotate, "clicked", G_CALLBACK(onRotate), d);
        const bool kbOn = keyboardOnOutput(out.name);
        GtkWidget* kb = key(std::string(kbOn ? "keyboard off" : "keyboard on"));
        g_object_set_data_full(G_OBJECT(kb), "output-name", g_strdup(out.name.c_str()), g_free);
        g_signal_connect(kb, "clicked", G_CALLBACK(onKeyboard), d);
        GtkWidget* keys = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_box_pack_start(GTK_BOX(keys), rotate, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(keys), kb, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(card), keys, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(d->setupDisplayRow), card, FALSE, FALSE, 0);
    }
    gtk_widget_show_all(d->setupDisplayRow);
}

void onRotate(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    const char* name = static_cast<const char*>(g_object_get_data(G_OBJECT(w), "output-name"));
    if (!name) return;
    setStatus(d, "Turning display...");
    pumpUi();
    std::string err;
    if (!rotatePiOutput(name, err)) setStatus(d, err);
    else setStatus(d, "Layout saved. Restart the app so its windows follow the displays.");
    rebuildDisplays(d);
}

void onKeyboard(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    const char* name = static_cast<const char*>(g_object_get_data(G_OBJECT(w), "output-name"));
    if (!name) return;
    const bool enable = !keyboardOnOutput(name);
    setStatus(d, enable ? std::string("Enabling keyboard on ") + name : std::string("Disabling keyboard"));
    pumpUi();
    std::string err;
    if (!setOnScreenKeyboard(name, enable, err)) setStatus(d, err);
    else setStatus(d, enable ? std::string("Keyboard on ") + name : "Keyboard off");
    rebuildDisplays(d);
}

void onResetLayout(GtkWidget*, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    setStatus(d, "Resetting layout...");
    pumpUi();
    std::string err;
    if (!resetPiLayout(err)) setStatus(d, err);
    else setStatus(d, "Layout saved. Restart the app so its windows follow the displays.");
    rebuildDisplays(d);
}

void onRemember(GtkWidget*, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    std::string sink = trimCopy(readCommand("pactl get-default-sink 2>/dev/null"));
    std::string address;
    if (bluetoothAddressFromSink(sink, address)) {
        d->state->bluetooth_audio_address = address;
        d->state->bluetooth_audio_name = bluetoothDeviceName(address);
        setStatus(d, "Remembered " + d->state->bluetooth_audio_name);
    } else {
        d->state->bluetooth_audio_address.clear();
        d->state->bluetooth_audio_name.clear();
        d->state->bluetooth_audio_autoconnect = false;
        setStatus(d, "No Bluetooth speaker is playing: nothing remembered");
    }
    ConfigFile::save(*d->state);
    showRememberedBluetooth(d);
    refreshAudioKeys(d);
}

void onBtConnect(GtkWidget*, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    if (!bluetoothAddressOk(d->state->bluetooth_audio_address)) return;
    d->state->bluetooth_audio_autoconnect = true;
    ConfigFile::save(*d->state);
    spawnBluetoothConnect(d->state->bluetooth_audio_address, true);
    setStatus(d, "Connecting remembered speaker");
}

void onBtDisconnect(GtkWidget*, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    const std::string& address = d->state->bluetooth_audio_address;
    if (!bluetoothAddressOk(address)) return;
    d->state->bluetooth_audio_autoconnect = false;
    ConfigFile::save(*d->state);
    std::string script =
        "bluetoothctl disconnect '" + address + "' >/dev/null 2>&1; "
        "bluetoothctl untrust '" + address + "' >/dev/null 2>&1; "
        "sink=$(pactl get-default-sink 2>/dev/null); "
        "case \"$sink\" in bluez_output.*) "
        "alt=$(pactl list short sinks 2>/dev/null | awk '!/bluez_output/ {print $2; exit}'); "
        "[ -n \"$alt\" ] && pactl set-default-sink \"$alt\" ;; esac";
    gchar* argv[] = { (gchar*)"bash", (gchar*)"-c", const_cast<gchar*>(script.c_str()), nullptr };
    g_spawn_async(nullptr, argv, nullptr, G_SPAWN_SEARCH_PATH, nullptr, nullptr, nullptr, nullptr);
    setStatus(d, "Disconnecting remembered speaker");
}

// One key per Wi-Fi network saved on the Pi: the box's own hotspot first, then
// the networks it can join (for the internet). The one in use is lit.
void onWifi(GtkWidget* w, gpointer p);

void rebuildWifi(AppData* d) {
    if (!d->setupWifiRow) return;
    GList* children = gtk_container_get_children(GTK_CONTAINER(d->setupWifiRow));
    for (GList* c = children; c; c = c->next) gtk_widget_destroy(GTK_WIDGET(c->data));
    g_list_free(children);
    const std::vector<SavedWifi> nets = savedWifiNetworks();
    if (nets.empty())
        gtk_box_pack_start(GTK_BOX(d->setupWifiRow), text("No saved Wi-Fi networks found"), FALSE, FALSE, 0);
    for (const SavedWifi& n : nets) {
        GtkWidget* k = key((n.hotspot ? "hotspot " : "join ") + n.name);
        if (n.active) addClass(k, "key-lit");
        g_object_set_data_full(G_OBJECT(k), "wifi-name", g_strdup(n.name.c_str()), g_free);
        g_object_set_data(G_OBJECT(k), "wifi-hotspot", GINT_TO_POINTER(n.hotspot ? 1 : 0));
        g_signal_connect(k, "clicked", G_CALLBACK(onWifi), d);
        gtk_box_pack_start(GTK_BOX(d->setupWifiRow), k, FALSE, FALSE, 0);
    }
    gtk_widget_show_all(d->setupWifiRow);
}

void onWifi(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    const char* name = static_cast<const char*>(g_object_get_data(G_OBJECT(w), "wifi-name"));
    if (!name) return;
    const bool hotspot = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "wifi-hotspot")) != 0;
    setStatus(d, std::string("Switching to ") + name + "...");
    pumpUi();
    std::string detail;
    if (useSavedWifi(name, detail) && !hotspot)
        detail += ". The hotspot is off: phones on it lose the box until you switch back.";
    setStatus(d, detail);
    rebuildWifi(d);
}

std::string shellQuote(const std::string& s) {
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
}

// As upstream's "Exit & Check for updates": save, open a terminal running the
// install's own update script (on a Rallybox install it pulls the box branch
// and rebuilds), then close. The crew restart the app when it has finished.
void onUpdate(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    if (!confirmYesNo(w, "Update", "This will exit the app and check for updates.\nThe box must be connected to the internet first.")) return;
    ConfigFile::save(*d->state);

    const std::string dir = appDirectory();
    const std::string script = "cd " + shellQuote(dir) +
        " && ./update; echo; echo 'Press Enter to close'; read -r";
    std::vector<std::string> args = {
        "systemd-run", "--user", "--collect", "--working-directory=" + dir
    };
    auto addEnv = [&](const char* name) {
        const char* value = std::getenv(name);
        if (value && *value) args.push_back(std::string("--setenv=") + name + "=" + value);
    };
    addEnv("WAYLAND_DISPLAY");
    addEnv("DISPLAY");
    addEnv("XDG_RUNTIME_DIR");
    addEnv("XDG_SESSION_TYPE");
    addEnv("HOME");
    args.push_back("lxterminal");
    args.push_back("--working-directory=" + dir);
    args.push_back("-e");
    args.push_back("bash -lc " + shellQuote(script));

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (auto& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);

    GError* error = nullptr;
    if (!g_spawn_async(dir.c_str(), argv.data(), nullptr, G_SPAWN_SEARCH_PATH,
                       nullptr, nullptr, nullptr, &error)) {
        setStatus(d, error ? error->message : "Could not open a terminal");
        if (error) g_error_free(error);
        return;
    }
    gtk_main_quit();
}

void onShowSetup(GtkWidget*, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    releaseKeypadTarget(d);
    gtk_stack_set_visible_child_name(d->copilotStack, "setup");
    rebuildDisplays(d);
    rebuildWifi(d);
    refreshAudioKeys(d);
    setStatus(d, wifiStatusLine());
}

void onBackToSettings(GtkWidget*, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    gtk_stack_set_visible_child_name(d->copilotStack, "settings");
}

}  // namespace

GtkWidget* createSpannerKey(AppData* d) {
    GtkWidget* k = gtk_button_new();
    gtk_widget_set_size_request(k, SPANNER_W, SPANNER_H);
    addClass(k, "dist-heading");
    addClass(k, "key-small");
    // The same theme icon as the upstream Setup key, at the menu key's scale.
    GtkWidget* icon = gtk_image_new_from_icon_name("preferences-system-symbolic", GTK_ICON_SIZE_BUTTON);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 40);
    gtk_button_set_image(GTK_BUTTON(k), icon);
    g_signal_connect(k, "clicked", G_CALLBACK(onShowSetup), d);
    return k;
}

void bluetoothAutoconnectOnStartup(AppData* d) {
    if (!d || !d->state->bluetooth_audio_autoconnect) return;
    if (!bluetoothAddressOk(d->state->bluetooth_audio_address)) return;
    spawnBluetoothConnect(d->state->bluetooth_audio_address, true);
}

GtkWidget* createSetupScreen(AppData* d) {
    GtkWidget* screen = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(screen), 5);

    GtkWidget* left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    // Panel width less the right column, the gap and the screen's own padding:
    // fixed, so the page can never widen the 1280 px window.
    // The column sits in a scrolled window so its content cannot widen the page.
    GtkWidget* hold = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(hold), GTK_POLICY_AUTOMATIC, GTK_POLICY_NEVER);
    gtk_widget_set_size_request(hold, nero::PANEL_W - nero::KEYPAD_COL_W - 12, -1);
    gtk_container_add(GTK_CONTAINER(hold), left);
    gtk_box_pack_start(GTK_BOX(screen), hold, FALSE, FALSE, 0);

    // title: SETUP, version, hostname
    GtkWidget* title = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gtk_box_pack_start(GTK_BOX(title), text("SETUP", "seg-head"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(title), text(std::string("version ") + nero::VERSION, "set-name"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(title), text("hostname " + piHostname(), "set-name"), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left), title, FALSE, FALSE, 0);

    // displays
    d->setupDisplayRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_box_pack_start(GTK_BOX(left), d->setupDisplayRow, FALSE, FALSE, 0);
    GtkWidget* reset = key("reset layout");
    gtk_widget_set_halign(reset, GTK_ALIGN_START);
    g_signal_connect(reset, "clicked", G_CALLBACK(onResetLayout), d);
    gtk_box_pack_start(GTK_BOX(left), reset, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), FALSE, FALSE, 2);

    // Bluetooth speaker
    gtk_box_pack_start(GTK_BOX(left), text("Bluetooth speaker", "seg-head"), FALSE, FALSE, 0);
    GtkWidget* bt = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* remember = key("remember playing speaker");
    g_signal_connect(remember, "clicked", G_CALLBACK(onRemember), d);
    d->bluetoothAudioLabel = GTK_LABEL(text(!d->state->bluetooth_audio_name.empty()
        ? d->state->bluetooth_audio_name : d->state->bluetooth_audio_address));
    gtk_widget_set_valign(GTK_WIDGET(d->bluetoothAudioLabel), GTK_ALIGN_CENTER);
    d->setupBtConnectBtn = key("connect");
    d->setupBtDisconnectBtn = key("disconnect");
    gtk_widget_set_no_show_all(d->setupBtConnectBtn, TRUE);
    gtk_widget_set_no_show_all(d->setupBtDisconnectBtn, TRUE);
    g_signal_connect(d->setupBtConnectBtn, "clicked", G_CALLBACK(onBtConnect), d);
    g_signal_connect(d->setupBtDisconnectBtn, "clicked", G_CALLBACK(onBtDisconnect), d);
    gtk_box_pack_start(GTK_BOX(bt), remember, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bt), GTK_WIDGET(d->bluetoothAudioLabel), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bt), d->setupBtConnectBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(bt), d->setupBtDisconnectBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(left), bt, FALSE, FALSE, 0);

    // Wi-Fi
    gtk_box_pack_start(GTK_BOX(left), text("Wi-Fi", "seg-head"), FALSE, FALSE, 0);
    d->setupWifiRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(left), d->setupWifiRow, FALSE, FALSE, 0);

    d->setupStatusLabel = GTK_LABEL(text(wifiStatusLine()));
    gtk_label_set_line_wrap(d->setupStatusLabel, TRUE);
    gtk_label_set_max_width_chars(d->setupStatusLabel, 60);
    gtk_box_pack_start(GTK_BOX(left), GTK_WIDGET(d->setupStatusLabel), FALSE, FALSE, 0);

    // Software update, at the foot of the page
    GtkWidget* update = key("check for software updates");
    gtk_widget_set_halign(update, GTK_ALIGN_START);
    g_signal_connect(update, "clicked", G_CALLBACK(onUpdate), d);
    gtk_box_pack_end(GTK_BOX(left), update, FALSE, FALSE, 0);

    // right column: back to Settings, and the menu key in its usual place
    GtkWidget* col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(col, nero::KEYPAD_COL_W, -1);
    gtk_box_pack_end(GTK_BOX(screen), col, FALSE, FALSE, 0);
    // "settings" (back) sits directly above the menu key, both right-aligned
    // and the menu key's width, so the column stays within its 240 px.
    GtkWidget* back = gtk_button_new_with_label("settings");
    gtk_widget_set_size_request(back, nero::MENU_KEY_W, nero::MENU_KEY_H);
    gtk_widget_set_halign(back, GTK_ALIGN_END);
    gtk_widget_set_margin_bottom(back, 10);
    addClass(back, "dist-heading");
    addClass(back, "key-small");
    g_signal_connect(back, "clicked", G_CALLBACK(onBackToSettings), d);
    GtkWidget* menu = createMenuKey(d);
    gtk_widget_set_halign(menu, GTK_ALIGN_END);
    gtk_box_pack_end(GTK_BOX(col), menu, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(col), back, FALSE, FALSE, 0);

    d->setupScreen = screen;
    return screen;
}
