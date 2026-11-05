#ifndef TEST_DRIVER_TRIAL_H
#define TEST_DRIVER_TRIAL_H

#include "test_framework.h"
#include "../driver_trial.h"
#include "../sector.h"
#include "../calculations.h"
#include "../rally_state.h"
#include <cmath>

// RB-DRV-18: the driver display inside an italian pc / pm. Same Mugello rows as test_sector.h
// (calibration 1,000,000: one count is one metre): navi 380 m; pc1 180 m in 31 s; pc2 60 m in 10 s ...
class TestDriverTrial {
    static Segment row(int type, double m, double pc_s, int ref, double kph = 0.0) {
        Segment s; s.type = type; s.distance_m = m; s.pc_duration_s = pc_s; s.ref = ref;
        s.target_speed_kph = kph;
        return s;
    }
    static RallyState sector(std::vector<Segment> segs) {
        RallyState s;
        s.calibration = 1000000;
        s.counters = true;
        s.rally_type = 2;
        s.segments = segs;
        applySectorRules(s.segments, 10800);
        s.stage_segments = s.segments;
        s.segment_current_number = 0;
        s.stage_complete = false;
        s.total_start_cntr1 = s.total_start_cntr2 = 0;
        s.total_start_time_ms = 0;
        s.segment_start_cntr1 = s.segment_start_cntr2 = 0;
        s.segment_start_time_ms = 0;
        s.sector_card_ms = 10800 * 1000L;
        return s;
    }
    static RallyState mugello() {
        return sector({ row(SEG_NAVI, 380, 0, 0, 30.0), row(SEG_PC, 180, 31, 1), row(SEG_PC, 60, 10, 2),
                        row(SEG_PC, 110, 19, 3), row(SEG_NAVI, 16500, 0, 0, 30.0) });
    }
    // In pc1: its tube at 380 m pressed at t = 1,000 s.
    static RallyState inPc1() {
        RallyState s = mugello();
        s.segment_current_number = 1;
        s.trial_start_ms = 1000000;
        s.trial_start_m = 380.0;
        return s;
    }
    static bool near(double a, double b, double tol = 0.01) { return std::fabs(a - b) < tol; }
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Driver display in a pc / pm (RB-DRV-18)");

        suite->addTest("final window: arrow progress 0 before 5 s, linear, 1 at and after the line", []() {
            return finalWindowProgress(12.0) == 0.0 && finalWindowProgress(5.0) == 0.0
                && near(finalWindowProgress(2.5), 0.5) && finalWindowProgress(0.0) == 1.0
                && finalWindowProgress(-3.0) == 1.0;
        });

        suite->addTest("countdown box: -.- until 5 s, then tenths, never negative", []() {
            return countdownBoxText(8.4) == "-.-" && countdownBoxText(5.04) == "-.-"
                && countdownBoxText(5.0) == "5.0" && countdownBoxText(4.37) == "4.4"
                && countdownBoxText(0.004) == "0.0" && countdownBoxText(-1.2) == "0.0";
        });

        suite->addTest("speeds 2 dp, sector total in km 2 dp", []() {
            return trialSpeedText(72.3) == "72.30" && trialSpeedText(0.0) == "0.00"
                && trialKmText(27477.0) == "27.48" && trialKmText(0.0) == "0.00";
        });

        suite->addTest("inside pc1: active, tag, countdown to its card time", []() {
            RallyState s = inPc1();
            DriverTrialView v = driverTrialView(s, 450, 450, 1000000 + 5000);
            return v.active && v.tag == "pc1" && v.isPc && near(v.toLineS, 31.0 - 5.0) && near(v.durationS, 31.0)
                && !v.boxLive && v.boxS == DRIVER_FINAL_S && v.arrowProgress == 0.0;
        });

        suite->addTest("inside pc1, 2.5 s to the line: box live, arrow half way", []() {
            RallyState s = inPc1();
            DriverTrialView v = driverTrialView(s, 550, 550, 1000000 + 28500);
            return v.active && v.boxLive && near(v.boxS, 2.5) && near(v.arrowProgress, 0.5);
        });

        suite->addTest("over the line late: box 0.00, arrow on the tick", []() {
            RallyState s = inPc1();
            DriverTrialView v = driverTrialView(s, 560, 560, 1000000 + 34000);
            return v.active && v.toLineS < 0.0 && v.boxS == 0.0 && v.arrowProgress == 1.0
                && countdownBoxText(v.toLineS) == "0.0";
        });

        suite->addTest("average over the pc from its tube, km/h even with mph set", []() {
            RallyState s = inPc1();
            DriverTrialView v = driverTrialView(s, 480, 480, 1000000 + 5000);   // 100 m in 5 s
            s.units = 1;
            DriverTrialView m = driverTrialView(s, 480, 480, 1000000 + 5000);
            DriverTrialView early = driverTrialView(s, 381, 381, 1000000 + 300);
            return v.avgValid && near(v.avgKph, 72.0) && near(m.avgKph, 72.0)
                && near(v.sectorTotalM, 480.0) && !early.avgValid;
        });

        suite->addTest("not active: greek, navi row, no tube clock (power loss), stage over", []() {
            RallyState g = inPc1(); g.rally_type = 0;
            RallyState n = inPc1(); n.segment_current_number = 0;
            RallyState p = inPc1(); p.trial_start_m = -1.0;
            RallyState d = inPc1(); d.stage_complete = true;
            const int64_t t = 1000000 + 5000;
            return !driverTrialView(g, 450, 450, t).active && !driverTrialView(n, 450, 450, t).active
                && !driverTrialView(p, 450, 450, t).active && !driverTrialView(d, 450, 450, t).active;
        });

        suite->addTest("pm of two rows: counts to the end of the row being driven", []() {
            // navi 380 m; pm1 1000 m at 36 km/h (100 s); pm1 again 500 m at 36 km/h (50 s).
            RallyState s = sector({ row(SEG_NAVI, 380, 0, 0, 30.0), row(SEG_PM, 1000, 0, 1, 36.0),
                                    row(SEG_PM, 500, 0, 1, 36.0) });
            s.segment_current_number = 1;
            s.trial_start_ms = 1000000;
            s.trial_start_m = 380.0;
            DriverTrialView v = driverTrialView(s, 480, 480, 1000000 + 10000);
            return v.active && v.tag == "pm1" && !v.isPc && near(v.toLineS, 100.0 - 10.0, 0.05);
        });

        suite->addTest("prev straight after next into pc1: the view follows back to the navi row", []() {
            RallyState s = mugello();
            const bool applied = applySectorNext(s, 385, 385, 70000);
            const bool in_pc = driverTrialView(s, 400, 400, 72000).active;
            const bool undone = undoSectorTube(s);
            return applied && in_pc && undone && !driverTrialView(s, 400, 400, 72000).active;
        });

        suite->addTest("right box legend: countdown in a pc, arrival on navi, none on greek", []() {
            return std::string(driverRightBoxLegend(DRIVER_PAGE_ITALIAN_TRIAL)) == "countdown"
                && std::string(driverRightBoxLegend(DRIVER_PAGE_ITALIAN_NAVI)) == "arrival"
                && std::string(driverRightBoxLegend(3)).empty()
                && std::string(driverRightBoxLegend(4)).empty()
                && std::string(driverRightBoxLegend(0)).empty();
        });

        return suite;
    }
};

#endif
