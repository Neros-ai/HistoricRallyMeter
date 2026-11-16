#ifndef GAUGE_REDRAW_H
#define GAUGE_REDRAW_H

#include "driver_trial.h"
#include <string>

// Everything the gauge's draw function (on_gauge_draw, ui_driver.cpp) reads, so the 10 ms tick
// queues a redraw only when something it shows has changed. Widget-free: filled by the tick from
// the widgets, compared here. Any field added to the draw must be added here too.
//
// Not covered by a field, on purpose:
//  - The italian navi page reads the rally clock while drawing (the countdown to the line under
//    the target): that page is time-driven and is redrawn on every tick (gaugeNeedsRedraw).
//  - Exposure (a window mapped, uncovered, a page switched to): GTK draws those itself.
struct GaugeDrawInputs {
    // The two drawing areas' sizes (GTK also redraws on a resize; kept here so a change can
    // never be missed).
    int driverW = 0, driverH = 0, copilotW = 0, copilotH = 0;
    bool compact = false;          // AppData::driverCompactMode
    bool embedded = false;         // AppData::singleDisplayMode
    // The tick (rally_tick.h TickDecision)
    double aheadBehindSeconds = 0.0;
    int driverPage = 0;
    bool inSegment = false;
    double segmentProgress = 0.0;
    bool trialActive = false;
    double trialArrowProgress = 0.0;
    double trialToLineS = 0.0;
    std::string trialTag;
    bool trialIsPc = false;
    long driverStageNo = 0;
    bool driverMinutesValid = false;
    bool driverMinutesBehind = false;
    long driverMinutes = 0;
    // The texts the tick worked out for the dial (AppData::DriverShown)
    std::string speed, target, targetCap, totalAvg, tripAvg, digital, totalDist, tripDist;
    int zone = 0;
    double maxSeconds = 0.0;
    // Settings the draw reads straight from the state
    int gaugeRangeS = 0;
    bool unitsMph = false;
    int distanceUnit = 0;
    bool italian = false;
    // The footer: the fps and cpu labels' texts
    std::string fpsText, cpuText;

    bool operator==(const GaugeDrawInputs&) const = default;
};

// True when the gauge must be redrawn: the first time, whenever any input differs from the last
// drawn, and on every tick while the page reads the clock as it draws.
inline bool gaugeNeedsRedraw(const GaugeDrawInputs& now, const GaugeDrawInputs& last, bool haveLast) {
    if (!haveLast) return true;
    if (now.driverPage == DRIVER_PAGE_ITALIAN_NAVI) return true;   // time-driven (see above)
    return !(now == last);
}

#endif // GAUGE_REDRAW_H
