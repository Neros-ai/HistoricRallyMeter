#ifndef DRIVER_TRIAL_H
#define DRIVER_TRIAL_H

// RB-DRV-18: the driver display inside an italian pc / pm. Everything the gauge draws in
// that layout, from the state, the counters and now. New code beside sector.cpp: it only
// calls the sector functions, never changes them. Mockup:
// tools/layout-preview/nero2/driver-italian-pc.html (agreed 2026-11-01).

#include <cstdint>
#include <string>
struct RallyState;   // forward only: rally_types.h includes this header for DriverTrialView

// The final window: the countdown box runs and the arrow slides only in the last 5 s.
const double DRIVER_FINAL_S = 5.0;

struct DriverTrialView {
    bool active = false;        // italian, a sector running, inside a pc / pm
    std::string tag;            // "pc1", "pm2"
    bool isPc = false;          // pc: red logo with a stopwatch; pm: yellow with a speedo
    double sectorTotalM = 0.0;  // sector total (sectorPositionM)
    double avgKph = 0.0;        // over this pc / pm from its start tube, always km/h
    bool avgValid = false;      // false until 0.5 s and > 0 m in
    double durationS = 0.0;     // whole seconds this pc / pm should take (to the line + elapsed)
    double toLineS = 0.0;       // seconds to the line, may be negative once over it
    double boxS = DRIVER_FINAL_S; // the box's figure: toLineS held to 0 .. 5
    bool boxLive = false;       // inside the final window (toLineS < 5)
    double arrowProgress = 0.0; // 0 before the window, 1 at the line and after
};

DriverTrialView driverTrialView(const RallyState& s, uint64_t c1, uint64_t c2, int64_t now_ms);
// 0 at >= 5 s to the line, 1 at <= 0 s, linear between.
double finalWindowProgress(double to_line_s);
// The countdown box: "5.0" .. "0.0" (1 dp), never negative, never above 5.0.
std::string countdownBoxText(double to_line_s);
// Speeds on this layout: km/h to 2 dp ("72.30").
std::string trialSpeedText(double kph);
// Sector total on this layout: km to 2 dp ("27.48"), as the italian navi page.
std::string trialKmText(double metres);

// RB-DRV-20: the small legend right of the green / red box. Page numbers as
// AppData::DP_* in rally_types.h (ui_driver.cpp static_asserts they match).
const int DRIVER_PAGE_ITALIAN_TRIAL = 1, DRIVER_PAGE_ITALIAN_NAVI = 2;
// "countdown" on the italian pc / pm page, "arrival" on the italian navi page, "" otherwise.
const char* driverRightBoxLegend(int driverPage);

#endif
