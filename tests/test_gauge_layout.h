#ifndef TEST_GAUGE_LAYOUT_H
#define TEST_GAUGE_LAYOUT_H

#include "test_framework.h"
#include "../calculations.h"
#include <cmath>
#include <string>

// Geometry tests for the compact (800x480) driver gauge layout. The same
// arithmetic is mirrored in tools/layout-preview/DriverWindowTextLayout.html;
// if that mockup is edited, these expectations move with it.
class TestGaugeLayout {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Compact Gauge Layout");

        suite->addTest("radius and centre match the 800x480 box", []() {
            CompactGaugeLayout L = computeCompactGaugeLayout(800, 480);
            // radius = min(width/2 - 25, height - 95) = min(375, 385) = 375
            return std::abs(L.radius - 375.0) < 0.001
                && std::abs(L.centerX - 400.0) < 0.001
                && std::abs(L.centerY - 405.0) < 0.001;
        });

        suite->addTest("fscale never exceeds 1.0", []() {
            // A gauge larger than the reference must not scale fonts UP, or
            // the values overflow the panel on a wide display.
            CompactGaugeLayout big = computeCompactGaugeLayout(1600, 900);
            CompactGaugeLayout small = computeCompactGaugeLayout(400, 240);
            return std::abs(big.fscale - 1.0) < 0.001 && small.fscale < 1.0;
        });

        suite->addTest("distance anchor sits left of centre, scaled by radius", []() {
            // Not mirrored off centerX like rightAnchor -- distanceAnchor is
            // where the "Distance (metres)" caption's own text ends up
            // right-aligning to, an absolute offset from the arc's own
            // radius, not a centre-relative gap.
            CompactGaugeLayout L = computeCompactGaugeLayout(800, 480);
            // radius = 375, so the arc-derived offset is 375 * 0.72 = 270;
            // RB-DRV-08 adds a flat +10 shift, giving 280.
            return std::abs(L.distanceAnchor - 280.0) < 0.001
                && L.distanceAnchor < L.centerX;
        });

        suite->addTest("distance anchor is shifted 10px right of the arc offset (RB-DRV-08)", []() {
            CompactGaugeLayout L = computeCompactGaugeLayout(800, 480);
            // radius = 375, so the un-shifted offset would be 270; RB-DRV-08
            // adds a flat +10.
            return std::abs(L.distanceAnchor - 280.0) < 0.001;
        });

        suite->addTest("curTopSize is 50 at full scale", []() {
            CompactGaugeLayout L = computeCompactGaugeLayout(800, 480);
            return std::abs(L.curTopSize - 50.0) < 0.001;
        });

        suite->addTest("band edges straddle the centre and sit above centerY", []() {
            // bandOuterX/bandTargetX mirror the hub by radius+6 (the arc's
            // outer edge, lineWidth 12 centred on `radius`); bandTopY is
            // that same radius+6 offset straight up.
            CompactGaugeLayout L = computeCompactGaugeLayout(800, 480);
            double expectedOuter = L.centerX + (L.radius + 6.0);
            double expectedTarget = L.centerX - (L.radius + 6.0);
            double expectedTopY = L.centerY - (L.radius + 6.0);
            return std::abs(L.bandOuterX - expectedOuter) < 0.001
                && std::abs(L.bandTargetX - expectedTarget) < 0.001
                && std::abs(L.bandTopY - expectedTopY) < 0.001
                && L.bandTopY < L.centerY;
        });

        suite->addTest("Total sits above Trip, separated by more than rowGap", []() {
            // RB-DRV-08: Target moved off this stack entirely (to the
            // top-left corner); Total/Trip are drawn at the larger
            // curTopSize with extra line spacing so the taller glyphs'
            // descenders clear the ahead/behind box above them -- so the
            // gap is now rowGap plus a scaled margin, not exactly rowGap.
            CompactGaugeLayout L = computeCompactGaugeLayout(800, 480);
            double gap = L.tripBaseline - L.totalBaseline;
            return L.totalBaseline < L.tripBaseline
                && gap > L.rowGap
                && gap < L.rowGap + 20.0 * L.fscale + 0.001;
        });

        suite->addTest("row gap is 48 at full scale", []() {
            CompactGaugeLayout L = computeCompactGaugeLayout(800, 480);
            return std::abs(L.rowGap - 48.0) < 0.001;
        });

        suite->addTest("curTopSize is the largest text size in the layout", []() {
            // RB-DRV-08: Current/Target moved to curTopSize, the single
            // largest font in the layout -- there is no more separate
            // curBaseline/targetBaseline row stack to compare rows against.
            CompactGaugeLayout L = computeCompactGaugeLayout(800, 480);
            return L.curTopSize > L.valSize && L.curTopSize > L.labelSize;
        });

        suite->addTest("Trip row clears the needle hub", []() {
            // The bottom row must not collide with the hub at centerY, and
            // must sit clearly above the ahead/behind box's top edge (which
            // covers the hub) -- RB-DRV-08 nudges Trip up specifically so
            // its descenders clear that box.
            CompactGaugeLayout L = computeCompactGaugeLayout(800, 480);
            return L.tripBaseline < L.centerY && L.tripBaseline < L.boxY;
        });

        suite->addTest("caption clears the Trip row above it", []() {
            CompactGaugeLayout L = computeCompactGaugeLayout(800, 480);
            return L.captionBaseline > L.tripBaseline;
        });

        suite->addTest("distance caption follows the metre/kilometre switch", []() {
            // The caption carries the unit, so it must track the automatic
            // switch formatDistanceAutoUnit() makes above 999,999 m -- a
            // hard-coded "metres" would misread a km value by 1000x.
            return distanceColumnCaption("m") == "Distance (metres)"
                && distanceColumnCaption("km") == "Distance (kilometres)";
        });

        suite->addTest("distance caption defaults to metres on a null unit", []() {
            return distanceColumnCaption(nullptr) == "Distance (metres)";
        });

        suite->addTest("speed caption follows the KPH/MPH setting", []() {
            return speedColumnCaption(false) == "Average Speed (Kmh)"
                && speedColumnCaption(true) == "Average Speed (Mph)";
        });

        suite->addTest("error box covers the needle hub", []() {
            // The box is lifted so its top edge is above the hub; drawn after
            // the needle it then paints the hub out, which is the point.
            CompactGaugeLayout L = computeCompactGaugeLayout(800, 480);
            return L.boxY < L.centerY
                && (L.boxY + L.boxHeight) > L.centerY;
        });

        suite->addTest("error box is 50px tall", []() {
            CompactGaugeLayout L = computeCompactGaugeLayout(800, 480);
            return std::abs(L.boxHeight - 50.0) < 0.001;
        });

        suite->addTest("footer sits on the bottom edge of the error box", []() {
            CompactGaugeLayout L = computeCompactGaugeLayout(800, 480);
            return std::abs(L.footBaseline - (L.boxY + L.boxHeight)) < 0.001;
        });

        suite->addTest("footer text never shrinks below 11px", []() {
            // Below ~11px the fps/cpu readout is unreadable on the box's
            // 800x480 panel at arm's length.
            CompactGaugeLayout tiny = computeCompactGaugeLayout(320, 200);
            return tiny.footSize >= 11.0;
        });

        // Re-enabled here at the RB-DRV-02/RB-DRV-03 merge point: this test
        // was deferred on RB-DRV-03 (a sibling of RB-DRV-02, both cut from
        // RB-DRV-01) since CompactGaugeLayout::captionBaseline only existed
        // on RB-DRV-02 until both branches landed on box.
        suite->addTest("captions clear the error box", []() {
            // The captions were previously top-aligned with the box. Now
            // the box has moved up over the hub, they must not follow it
            // there.
            CompactGaugeLayout L = computeCompactGaugeLayout(800, 480);
            return L.captionBaseline > L.boxY;
        });

        // Needle geometry tests -- from RB-DRV-04/05, a separate lineage cut
        // from main directly (no CompactGaugeLayout on that side), merged
        // into this same suite/file at the RB-DRV land point.
        suite->addTest("needle points straight up at zero error", []() {
            NeedleGeometry n = computeNeedleGeometry(0.0, 10.0, 375.0);
            // Cairo's y grows downward, so straight up is 3*PI/2.
            return std::abs(n.angle - (3.0 * M_PI / 2.0)) < 1e-9;
        });

        suite->addTest("needle reaches the scale ends at full deflection", []() {
            NeedleGeometry pos = computeNeedleGeometry(10.0, 10.0, 375.0);
            NeedleGeometry neg = computeNeedleGeometry(-10.0, 10.0, 375.0);
            return std::abs(pos.angle - 2.0 * M_PI) < 1e-9
                && std::abs(neg.angle - M_PI) < 1e-9;
        });

        suite->addTest("needle pegs rather than sweeping past the scale", []() {
            // Beyond full deflection the needle stops; the digital readout
            // carries the real magnitude.
            NeedleGeometry way_over = computeNeedleGeometry(600.0, 10.0, 375.0);
            NeedleGeometry at_max = computeNeedleGeometry(10.0, 10.0, 375.0);
            return std::abs(way_over.angle - at_max.angle) < 1e-9;
        });

        suite->addTest("needle tip stops short of the tick ring", []() {
            NeedleGeometry n = computeNeedleGeometry(0.0, 10.0, 375.0);
            return n.length < 375.0 - 10.0 && n.length > 0.0;
        });

        suite->addTest("needle is a constant-width bar matching the ticks", []() {
            NeedleGeometry a = computeNeedleGeometry(0.0, 10.0, 375.0);
            NeedleGeometry b = computeNeedleGeometry(7.5, 10.0, 375.0);
            return std::abs(a.halfWidth - 3.0) < 1e-9
                && std::abs(a.halfWidth - b.halfWidth) < 1e-9;
        });

        suite->addTest("effective max stays at 3 within the inner zone", []() {
            // Matches pristine's green ±3s scale exactly while deflecting.
            return std::abs(gaugeEffectiveMaxSeconds(1.5) - 3.0) < 1e-9
                && std::abs(gaugeEffectiveMaxSeconds(-2.9) - 3.0) < 1e-9;
        });

        suite->addTest("effective max tracks the reading beyond 3, capped at 30", []() {
            // This is what makes the needle sit pinned at the edge past 3s
            // with no separate clamp: seconds/effectiveMax is always exactly
            // +-1 here, by construction.
            return std::abs(gaugeEffectiveMaxSeconds(15.0) - 15.0) < 1e-9
                && std::abs(gaugeEffectiveMaxSeconds(-15.0) - 15.0) < 1e-9
                && std::abs(gaugeEffectiveMaxSeconds(45.0) - 30.0) < 1e-9
                && std::abs(gaugeEffectiveMaxSeconds(-100.0) - 30.0) < 1e-9;
        });

        suite->addTest("zone follows the 10s and 30s boundaries, sign-independent", []() {
            return gaugeZone(5.0) == 0 && gaugeZone(9.9) == 0
                && gaugeZone(10.0) == 1 && gaugeZone(29.9) == 1
                && gaugeZone(30.0) == 2 && gaugeZone(-30.0) == 2;
        });

        suite->addTest("arc colour matches pristine's per-scale values", []() {
            GaugeArcColor green = gaugeArcColor(0);
            GaugeArcColor amber = gaugeArcColor(1);
            GaugeArcColor red   = gaugeArcColor(2);
            return std::abs(green.g - 0.7) < 1e-9 && green.r < 1e-9
                && std::abs(amber.r - 0.85) < 1e-9 && std::abs(amber.g - 0.65) < 1e-9
                && std::abs(red.r - 0.8) < 1e-9 && std::abs(red.g - 0.1) < 1e-9;
        });

        suite->addTest("tick label is signed, blank at zero", []() {
            // The centre is marked by the triangle above the arc; a "0"
            // there would sit under the needle at rest.
            return gaugeTickLabel(0).empty()
                && gaugeTickLabel(-7) == "-7"
                && gaugeTickLabel(12) == "12";
        });

        suite->addTest("tick labels are visible only in the green zone", []() {
            return gaugeTickLabelsVisible(5.0)
                && !gaugeTickLabelsVisible(10.0)
                && !gaugeTickLabelsVisible(30.0)
                && !gaugeTickLabelsVisible(-15.0);
        });

        suite->addTest("tick labels follow the hysteretic zone, not the raw reading", []() {
            // The gauge draws its arc, digits and chevrons from the damped
            // zone, so the numerals must come from the same one. Taking
            // them from the raw reading left 9.5-10.0 on the way down out of
            // amber showing green numerals against an amber arc, and left a
            // reading parked on 10.0 flickering them on every redraw.
            return gaugeTickLabelsVisibleInZone(0)
                && !gaugeTickLabelsVisibleInZone(1)
                && !gaugeTickLabelsVisibleInZone(2)
                // 9.7 still reads as zone 1 while descending out of amber.
                && !gaugeTickLabelsVisibleInZone(gaugeZoneHysteretic(9.7, 1))
                // ...and as zone 0 once it has cleared the dead band.
                && gaugeTickLabelsVisibleInZone(gaugeZoneHysteretic(9.3, 1))
                // The raw form still agrees with the plain zone.
                && gaugeTickLabelsVisible(5.0) == gaugeTickLabelsVisibleInZone(gaugeZone(5.0));
        });

        suite->addTest("tick angle matches the needle angle for the same seconds value", []() {
            // A tick labelled i seconds must sit at the same angle the
            // needle would be at for a reading of exactly i seconds -- the
            // whole point of numbering the ticks at all.
            double max_val = 10.0;
            for (int i = -10; i <= 10; i++) {
                NeedleGeometry n = computeNeedleGeometry(static_cast<double>(i), max_val, 375.0);
                double tick_angle = gaugeTickAngle(i, max_val);
                if (std::abs(n.angle - tick_angle) > 1e-9) return false;
            }
            return true;
        });

        suite->addTest("tick angle is exact even when max_val is fractional", []() {
            // Reviewer's worked example: reading = 3.9s -> max_val = 3.9,
            // tick_count (truncated) = 3. Before the fix, tick "3" was
            // placed at angle = i/tick_count of the sweep, i.e. 3.9
            // seconds' worth of deflection instead of 3 -- a 0.9s error.
            // gaugeTickAngle() must divide by max_val (3.9), not
            // tick_count (3), so tick "3" lands exactly where a genuine
            // 3-second reading would put the needle.
            double max_val = 3.9;
            NeedleGeometry three_sec_needle = computeNeedleGeometry(3.0, max_val, 375.0);
            double tick_three_angle = gaugeTickAngle(3, max_val);
            return std::abs(three_sec_needle.angle - tick_three_angle) < 1e-9;
        });

        // ---- zone hysteresis ----
        // gaugeZone() is recomputed every frame from the raw reading and
        // drives the digital format, the arc colour, the chevron count and
        // the tick labels. Without a dead band a reading sitting on a
        // boundary alternates on every redraw of the 10ms timer.

        suite->addTest("a reading hovering on the 30s boundary does not flip zone", []() {
            // Was zone 2 (>=30); a hair under 30 must not drop straight back.
            ASSERT_EQ(gaugeZoneHysteretic(29.98, 2), 2);
            // ...but a clear move below the dead band does.
            ASSERT_EQ(gaugeZoneHysteretic(29.0, 2), 1);
            return true;
        });

        suite->addTest("a reading hovering on the 10s boundary does not flip zone", []() {
            ASSERT_EQ(gaugeZoneHysteretic(9.98, 1), 1);
            ASSERT_EQ(gaugeZoneHysteretic(9.0, 1), 0);
            return true;
        });

        suite->addTest("entering a zone still uses the plain boundary", []() {
            // Hysteresis widens the exit, never the entry: the gauge must
            // change the instant the reading genuinely crosses.
            ASSERT_EQ(gaugeZoneHysteretic(10.0, 0), 1);
            ASSERT_EQ(gaugeZoneHysteretic(30.0, 1), 2);
            return true;
        });

        suite->addTest("hysteresis is symmetric for negative readings", []() {
            ASSERT_EQ(gaugeZoneHysteretic(-29.98, 2), 2);
            ASSERT_EQ(gaugeZoneHysteretic(-10.0, 0), 1);
            return true;
        });

        suite->addTest("an out-of-range previous zone falls back to the plain zone", []() {
            ASSERT_EQ(gaugeZoneHysteretic(29.98, 99), gaugeZone(29.98));
            return true;
        });

        return suite;
    }
};

#endif // TEST_GAUGE_LAYOUT_H
