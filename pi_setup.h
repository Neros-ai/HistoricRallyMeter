#ifndef PI_SETUP_H
#define PI_SETUP_H

#include <string>
#include <vector>

struct PiMode {
    int w = 0;
    int h = 0;
    double refresh = 0;
    bool preferred = false;
    bool current = false;
};

struct PiOutput {
    std::string name;
    int mode_w = 0;
    int mode_h = 0;
    double refresh = 0;
    int x = 0;
    int y = 0;
    std::string transform = "normal";
    double scale = 1;
    std::vector<PiMode> modes;

    int logicalW() const;
    int logicalH() const;
    std::string orientationName() const;
};

std::vector<PiOutput> readPiOutputs();
std::string cycleTransform(const std::string& current);

// Apply with wlr-randr and write the matching kanshi profile. error is empty on success.
bool applyPiLayout(const std::vector<PiOutput>& outputs, std::string& error);
bool rotatePiOutput(const std::string& name, std::string& error);
bool resetPiLayout(std::string& error);

std::string piHostname();
std::string wifiClientSsid();
bool startWifiHotspot(std::string& detail);
bool joinWifiClient(std::string& detail);
std::string wifiStatusLine();

// A Wi-Fi network saved on the Pi (NetworkManager connection).
struct SavedWifi {
    std::string name;
    bool hotspot = false;   // the box broadcasts it (mode ap)
    bool active = false;
};
// Every saved Wi-Fi connection, hotspots first. Empty when nmcli is missing.
std::vector<SavedWifi> savedWifiNetworks();
// Bring one up, leaving every saved setting (autoconnect, priority) alone.
bool useSavedWifi(const std::string& name, std::string& detail);

// True when the on-screen keyboard is enabled and pinned to this output.
bool keyboardOnOutput(const std::string& output);
// enable pins squeekboard to output and starts it. Otherwise the keyboard is turned off.
bool setOnScreenKeyboard(const std::string& output, bool enable, std::string& error);

#endif
