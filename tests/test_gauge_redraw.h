#ifndef TEST_GAUGE_REDRAW_H
#define TEST_GAUGE_REDRAW_H

#include "test_framework.h"
#include "../gauge_redraw.h"
#include "../rally_tick.h"
#include "../calculations.h"
#include "../driver_trial.h"
#include <cstdlib>
#include <cmath>
#include <functional>
#include <vector>

// The gauge is queued for a redraw only when something its draw function reads has changed
// (gauge_redraw.h); a stale gauge is far worse than a wasted redraw.
class TestGaugeRedraw {
    static GaugeDrawInputs base() {
        GaugeDrawInputs in;
        in.driverW = 800; in.driverH = 480;
        in.compact = true;
        in.driverPage = DRIVER_PAGE_GREEK_IDLE;
        in.speed = "0.0"; in.target = "--.-"; in.targetCap = "Target";
        in.totalAvg = in.tripAvg = "--.--"; in.digital = "+0.0";
        in.totalDist = in.tripDist = "0.00";
        in.maxSeconds = 3.0; in.gaugeRangeS = 3;
        in.fpsText = "fps: 100"; in.cpuText = "cpu: 50C";
        return in;
    }

public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Gauge Redraw Tests");

        suite->addTest("the first tick always redraws", []() {
            ASSERT_TRUE(gaugeNeedsRedraw(base(), GaugeDrawInputs{}, false));
            ASSERT_TRUE(gaugeNeedsRedraw(base(), base(), false));
            return true;
        });

        suite->addTest("nothing shown changed: no redraw", []() {
            ASSERT_FALSE(gaugeNeedsRedraw(base(), base(), true));
            return true;
        });

        suite->addTest("any one input changing redraws", []() {
            const std::vector<std::function<void(GaugeDrawInputs&)>> changes = {
                [](GaugeDrawInputs& i) { i.driverW = 801; },
                [](GaugeDrawInputs& i) { i.driverH = 481; },
                [](GaugeDrawInputs& i) { i.copilotW = 10; },
                [](GaugeDrawInputs& i) { i.copilotH = 10; },
                [](GaugeDrawInputs& i) { i.compact = false; },
                [](GaugeDrawInputs& i) { i.embedded = true; },
                [](GaugeDrawInputs& i) { i.aheadBehindSeconds = 0.0001; },
                [](GaugeDrawInputs& i) { i.driverPage = DRIVER_PAGE_GREEK_STAGE; },
                [](GaugeDrawInputs& i) { i.inSegment = true; },
                [](GaugeDrawInputs& i) { i.segmentProgress = 0.5; },
                [](GaugeDrawInputs& i) { i.trialActive = true; },
                [](GaugeDrawInputs& i) { i.trialArrowProgress = 0.1; },
                [](GaugeDrawInputs& i) { i.trialToLineS = 4.9; },
                [](GaugeDrawInputs& i) { i.trialTag = "pc1"; },
                [](GaugeDrawInputs& i) { i.trialIsPc = true; },
                [](GaugeDrawInputs& i) { i.driverStageNo = 2; },
                [](GaugeDrawInputs& i) { i.driverMinutesValid = true; },
                [](GaugeDrawInputs& i) { i.driverMinutesBehind = true; },
                [](GaugeDrawInputs& i) { i.driverMinutes = -1; },
                [](GaugeDrawInputs& i) { i.speed = "0.1"; },
                [](GaugeDrawInputs& i) { i.target = "60.00"; },
                [](GaugeDrawInputs& i) { i.targetCap = "Arrival (kph)"; },
                [](GaugeDrawInputs& i) { i.totalAvg = "1.00"; },
                [](GaugeDrawInputs& i) { i.tripAvg = "1.00"; },
                [](GaugeDrawInputs& i) { i.digital = "+0.1"; },
                [](GaugeDrawInputs& i) { i.totalDist = "0.01"; },
                [](GaugeDrawInputs& i) { i.tripDist = "0.01"; },
                [](GaugeDrawInputs& i) { i.zone = 1; },
                [](GaugeDrawInputs& i) { i.maxSeconds = 3.5; },
                [](GaugeDrawInputs& i) { i.gaugeRangeS = 1; },
                [](GaugeDrawInputs& i) { i.unitsMph = true; },
                [](GaugeDrawInputs& i) { i.distanceUnit = 1; },
                [](GaugeDrawInputs& i) { i.italian = true; },
                [](GaugeDrawInputs& i) { i.fpsText = "fps: 99"; },
                [](GaugeDrawInputs& i) { i.cpuText = "cpu: 51C"; },
            };
            for (const auto& change : changes) {
                GaugeDrawInputs now = base();
                change(now);
                ASSERT_TRUE(gaugeNeedsRedraw(now, base(), true));
            }
            return true;
        });

        suite->addTest("the italian navi page reads the clock while drawing: redrawn every tick", []() {
            GaugeDrawInputs in = base();
            in.driverPage = DRIVER_PAGE_ITALIAN_NAVI;
            ASSERT_TRUE(gaugeNeedsRedraw(in, in, true));
            return true;
        });

        // web_telemetry.cpp: the italian navi page's "avg" used to be read back out of the driver's
        // Trip label (atof) and formatted again; it is now formatted from the tick's figure.
        suite->addTest("telemetry navi avg: the tick figure gives what the Trip label read-back gave", []() {
            const double xs[] = { 0.0, -0.0, 0.004, 0.005, 0.0049999, 12.345, 12.3449999, 12.355, 59.995,
                                  99.999, 123.456789, 1234.5, -3.215, 1e-9, 7.0 / 3.0, 250.0 };
            for (double x : xs) {
                const std::string label = averageSpeedText(x);   // what updateDriverDisplay sets
                ASSERT_EQ(trialSpeedText(std::atof(label.c_str())), trialSpeedText(x));
            }
            for (int i = 0; i < 200000; ++i) {   // a sweep across every hundredth's rounding edge
                const double x = i * 0.0005 + 1e-7 * (i % 7);
                ASSERT_EQ(trialSpeedText(std::atof(averageSpeedText(x).c_str())), trialSpeedText(x));
            }
            // An early departure counting down: the label reads --.--, which was always sent as 0.
            ASSERT_EQ(trialSpeedText(std::atof("--.--")), trialSpeedText(0.0));
            return true;
        });

        return suite;
    }
};

#endif // TEST_GAUGE_REDRAW_H
