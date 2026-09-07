#include "ui_driver.h"
#include "calculations.h"
#include "rally_types.h"
#include "rally_state.h"
#include "counter_poller.h"
#include "tone_generator.h"
#include "callbacks.h"
#include "simple_tone.h"
#include "arrow_tone.h"
#include <iomanip>
#include <sstream>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <fstream>
#include <string>

static std::string readCpuTemp() {
    std::ifstream f("/sys/class/thermal/thermal_zone0/temp");
    if (!f.is_open()) return "cpu: --C";
    int millideg = 0;
    f >> millideg;
    char buf[16];
    snprintf(buf, sizeof(buf), "cpu: %dC", millideg / 1000);
    return buf;
}

// Format the digital readout based on scale. `scale` here is the caller's
// zone number (see gaugeZone()) -- not a persisted scale field -- but the
// formatting itself is intentionally unchanged from pristine:
// Red (±5min): ±hhh:mm:ss   Yellow/Green (±10s/±3s): ±ss.s
static void formatGaugeDigital(char* buf, size_t bufsize, double seconds, int scale) {
    double abs_sec = std::abs(seconds);
    const char* sign = seconds < 0 ? "-" : "+";

    if (scale == 2) {
        int total_sec = static_cast<int>(abs_sec + 0.5);
        int h = total_sec / 3600;
        int m = (total_sec % 3600) / 60;
        int s = total_sec % 60;
        if (h > 0)
            snprintf(buf, bufsize, "%s%d:%02d:%02d", sign, h, m, s);
        else
            snprintf(buf, bufsize, "%s%02d:%02d", sign, m, s);
    } else {
        snprintf(buf, bufsize, "%s%.1f", sign, abs_sec);
    }
}

// Draw the rally gauge (GaugePilot RallyMaster style) with auto-scaling
gboolean on_gauge_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);

    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    double width = alloc.width;
    double height = alloc.height;
    // Compact layout: gauge fills the panel width (bezel ~18px + small margin);
    // the arc top may overlap the target text, which is drawn on top of it.
    // The hub sits low, leaving just enough room for the readout box + footer.
    // Wide layout keeps larger margins for the scale labels alongside the
    // left speeds pane.
    double radius = data->driverCompactMode
        ? std::min(width / 2 - 25, height - 95)
        : (std::min(width / 2, height) - 25) * 0.8;
    double centerX = data->driverCompactMode ? width / 2 : width - radius - 20;
    double centerY = data->driverCompactMode ? height - 75 : (height + radius) / 2;

    // Every quantity below is a pure function of this one reading,
    // recomputed fresh every frame -- there is no discrete "scale" to
    // debounce or switch between any more.
    double seconds = data->aheadBehindSeconds;
    double max_val = gaugeEffectiveMaxSeconds(seconds);
    // Hysteretic: gaugeZone() alone is recomputed every frame and drives the
    // digital format, arc colour, chevron count and tick labels, so a reading
    // sitting on 10.0 or 30.0 flickered all four on every 10ms redraw.
    int zone = gaugeZoneHysteretic(seconds, data->gaugeZoneShown);
    data->gaugeZoneShown = zone;
    GaugeArcColor arc = gaugeArcColor(zone);

    // Needle bar half-width, declared here so the ticks can match it.
    constexpr double NEEDLE_HALF_WIDTH = 3.0;

    // Font scale, 1.0 at the reference gauge radius. Used by the tick
    // numerals here and by the value rows in the compact branch below.
    constexpr double REF_RADIUS = 256.0;
    double fscale = std::min(1.0, radius / REF_RADIUS);

    // Background
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
    cairo_paint(cr);

    // Outer bezel ring
    cairo_set_source_rgb(cr, 0.25, 0.25, 0.25);
    cairo_set_line_width(cr, 4);
    cairo_arc(cr, centerX, centerY, radius + 18, M_PI, 2 * M_PI);
    cairo_stroke(cr);

    // Arc background
    cairo_set_source_rgb(cr, 0.12, 0.12, 0.12);
    cairo_set_line_width(cr, 28);
    cairo_arc(cr, centerX, centerY, radius, M_PI, 2 * M_PI);
    cairo_stroke(cr);

    // Coloured graduated arc (green/amber/red depending on the continuous
    // zone the reading currently falls in -- see gaugeZone()).
    int arc_segments = 40;
    for (int i = 0; i < arc_segments; i++) {
        double frac = -1.0 + (2.0 * i) / arc_segments;
        double angle = M_PI + M_PI/2 + frac * (M_PI / 2);
        double next_frac = -1.0 + (2.0 * (i + 1)) / arc_segments;
        double next_angle = M_PI + M_PI/2 + next_frac * (M_PI / 2);

        cairo_set_source_rgb(cr, arc.r, arc.g, arc.b);
        cairo_set_line_width(cr, 12);
        cairo_arc(cr, centerX, centerY, radius, angle, next_angle);
        cairo_stroke(cr);
    }

    // One tick per second of the effective sweep -- 3 per side while
    // deflecting, growing 1:1 with the reading beyond that, capped at 30.
    // Every tick is the same width (matching the needle bar RB-DRV-04
    // defines); pristine's separate, thinner "minor tick" category is gone.
    // Numerals are drawn only in the green zone -- past that the arc
    // colour is the at-a-glance signal instead.
    bool labels_visible = gaugeTickLabelsVisible(seconds);
    int tick_count = static_cast<int>(max_val);
    for (int i = -tick_count; i <= tick_count; i++) {
        // gaugeTickAngle() uses max_val (the true sweep end), matching
        // computeNeedleGeometry()'s seconds/max_seconds mapping -- not
        // tick_count, which is max_val truncated to a whole number of
        // ticks. See its header comment for why dividing by tick_count
        // instead would misplace ticks whenever max_val is fractional.
        double angle = gaugeTickAngle(i, max_val);

        double x1 = centerX + (radius - 20) * cos(angle);
        double y1 = centerY + (radius - 20) * sin(angle);
        double x2 = centerX + (radius + 8) * cos(angle);
        double y2 = centerY + (radius + 8) * sin(angle);

        cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
        cairo_set_line_width(cr, NEEDLE_HALF_WIDTH * 2);
        cairo_move_to(cr, x1, y1);
        cairo_line_to(cr, x2, y2);
        cairo_stroke(cr);

        if (!labels_visible) continue;
        std::string label = gaugeTickLabel(i);
        if (label.empty()) continue;

        // Rotated to follow the dial like clock-face numerals -- upright at
        // the top, tilting further out toward the sides. Horizontal
        // numerals read as increasingly skewed the further round the arc
        // they sit.
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 24 * fscale);

        cairo_text_extents_t extents;
        cairo_text_extents(cr, label.c_str(), &extents);
        double label_r = radius - 38;
        double lx = centerX + label_r * cos(angle);
        double ly = centerY + label_r * sin(angle);

        cairo_save(cr);
        cairo_translate(cr, lx, ly);
        cairo_rotate(cr, angle + M_PI / 2);
        // Centre the glyph ink on the origin, not its advance box, so a
        // one-digit and a two-digit numeral sit on the same radius.
        cairo_move_to(cr, -extents.width / 2 - extents.x_bearing,
                          -extents.height / 2 - extents.y_bearing);
        cairo_show_text(cr, label.c_str());
        cairo_restore(cr);
    }

    // Center triangle marker at 0
    cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
    double tri_y = centerY - radius - 12;
    cairo_move_to(cr, centerX, tri_y + 10);
    cairo_line_to(cr, centerX - 6, tri_y);
    cairo_line_to(cr, centerX + 6, tri_y);
    cairo_close_path(cr);
    cairo_fill(cr);

    char digital[24];
    formatGaugeDigital(digital, sizeof(digital), seconds, zone);

    // Needle: a constant-width bar rather than a tapered triangle, so its
    // edges stay parallel to the major ticks all the way out and the driver
    // reads a tick number instead of estimating a direction.
    NeedleGeometry needle = computeNeedleGeometry(seconds, max_val, radius);
    double needle_angle = needle.angle;
    double needle_length = needle.length;

    double dir_x = cos(needle_angle);
    double dir_y = sin(needle_angle);
    double perp_x = -sin(needle_angle);
    double perp_y = cos(needle_angle);
    double tip_x = centerX + needle_length * dir_x;
    double tip_y = centerY + needle_length * dir_y;

    cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
    cairo_move_to(cr, tip_x + needle.halfWidth * perp_x, tip_y + needle.halfWidth * perp_y);
    cairo_line_to(cr, tip_x - needle.halfWidth * perp_x, tip_y - needle.halfWidth * perp_y);
    cairo_line_to(cr, centerX - needle.halfWidth * perp_x, centerY - needle.halfWidth * perp_y);
    cairo_line_to(cr, centerX + needle.halfWidth * perp_x, centerY + needle.halfWidth * perp_y);
    cairo_close_path(cr);
    cairo_fill(cr);

    // Hub: a plain filled disc in the needle's own colour. The old ring and
    // inner dot were decoration on the one part of the dial that carries no
    // reading -- and in compact mode the readout box covers it anyway.
    cairo_arc(cr, centerX, centerY, 8, 0, 2 * M_PI);
    cairo_fill(cr);

    // Digital ahead/behind readout. Drawn after the needle and hub so that in
    // compact mode it paints the hub out: the hub carries no information, and
    // this readout is the one thing the driver looks at, so it takes the
    // centre of the dial. The wide layout keeps the original smaller box
    // below the hub, where there is nothing to cover.
    {
        CompactGaugeLayout L{};
        if (data->driverCompactMode) {
            L = computeCompactGaugeLayout(width, height);
        }
        double digital_size = data->driverCompactMode ? L.valSize : 22.0;
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, digital_size);

        cairo_text_extents_t dext;
        cairo_text_extents(cr, digital, &dext);

        double box_width, box_height, box_y, border;
        if (data->driverCompactMode) {
            // 10% wider than the fitted/minimum width, at the user's
            // request (matches the mockup's own `* 1.1` factor) -- applied
            // to the final computed width so it scales whichever branch
            // (the 180px floor or the digit-fitted width) is in effect.
            box_width  = std::max(180.0, dext.x_advance + 24) * 1.1;
            box_height = digital_size + 20.0;  // RB-DRV-08: fit-to-font, symmetric about the old box centre
            double box_center_y = L.boxY + L.boxHeight / 2.0;
            box_y = box_center_y - box_height / 2.0;
            border = 3.0;
        } else {
            box_width  = std::max(130.0, dext.x_advance + 16);
            box_height = 36.0;
            box_y      = centerY + 18;
            border     = 2.0;
        }
        double box_x = centerX - box_width / 2;

        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_rectangle(cr, box_x, box_y, box_width, box_height);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_set_line_width(cr, border);
        cairo_rectangle(cr, box_x, box_y, box_width, box_height);
        cairo_stroke(cr);

        if (data->driverCompactMode) {
            // RB-DRV-08: centre the "." on the box's horizontal centre (not
            // the whole string, which reads off-centre because of the
            // leading sign), and centre the digits vertically using the
            // glyphs' actual ink bounds rather than a hand-picked offset --
            // canvas/cairo baseline positioning is a font-metric
            // approximation that reads slightly off for bold monospace.
            double box_center_y = box_y + box_height / 2.0;
            const char* dot = strchr(digital, '.');
            double text_start_x;
            if (dot == nullptr) {
                text_start_x = centerX - dext.x_advance / 2.0;
            } else {
                std::string before_dot(digital, dot - digital);
                cairo_text_extents_t before_ext, dot_ext;
                cairo_text_extents(cr, before_dot.c_str(), &before_ext);
                cairo_text_extents(cr, ".", &dot_ext);
                text_start_x = centerX - before_ext.x_advance - dot_ext.x_advance / 2.0;
            }
            double baseline_y = box_center_y + (dext.height / 2.0 - dext.y_bearing - dext.height);
            // dext.y_bearing is negative (extends upward from baseline);
            // ascent = -y_bearing, descent = height + y_bearing.
            double ascent = -dext.y_bearing;
            double descent = dext.height + dext.y_bearing;
            baseline_y = box_center_y + (ascent - descent) / 2.0;
            cairo_move_to(cr, text_start_x, baseline_y);
            cairo_show_text(cr, digital);
        } else {
            cairo_move_to(cr, centerX - dext.width / 2,
                              box_y + box_height / 2 + dext.height / 2 - 2);
            cairo_show_text(cr, digital);
        }
    }

    // Scale chevrons + segment-end tick along the needle.
    // The number of chevrons encodes the current zone (green=1, amber=2, red=3).
    // The topmost chevron starts 20% out from the hub; while within a segment the
    // whole group slides toward the segment-end tick (20% from the tip) in
    // proportion to how much of the segment has been driven. Outside a segment the
    // chevrons stay at the start position and the tick is hidden.
    {
        int num_chevrons = zone + 1;  // 0->1 (green), 1->2 (amber), 2->3 (red)
        if (num_chevrons < 1) num_chevrons = 1;
        if (num_chevrons > 3) num_chevrons = 3;

        const double f_start = 0.20;  // topmost chevron start, fraction from hub
        const double f_tick  = 0.80;  // segment-end tick, 20% from the tip
        double progress = data->inSegment ? data->segmentProgress : 0.0;
        if (progress < 0.0) progress = 0.0;
        if (progress > 1.0) progress = 1.0;
        double f_top = f_start + progress * (f_tick - f_start);

        const double chevron_half_w = 18.0;  // 36px wide
        const double chevron_depth  = 14.0;  // along the needle
        const double spacing_px     = 16.0;  // between stacked chevrons
        double spacing_f = spacing_px / needle_length;

        // Segment-end tick: 48px perpendicular line, only while within a segment.
        if (data->inSegment) {
            double tx = centerX + f_tick * needle_length * dir_x;
            double ty = centerY + f_tick * needle_length * dir_y;
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_set_line_width(cr, 3.0);
            cairo_move_to(cr, tx + chevron_half_w * perp_x, ty + chevron_half_w * perp_y);
            cairo_line_to(cr, tx - chevron_half_w * perp_x, ty - chevron_half_w * perp_y);
            cairo_stroke(cr);
        }

        cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_ROUND);
        for (int i = 0; i < num_chevrons; i++) {
            double f = f_top - i * spacing_f;
            if (f < 0.03) break;  // keep clear of the hub
            double ax = centerX + f * needle_length * dir_x;      // apex (points to tip)
            double ay = centerY + f * needle_length * dir_y;
            double bx = ax - chevron_depth * dir_x;               // base, toward hub
            double by = ay - chevron_depth * dir_y;
            double e1x = bx + chevron_half_w * perp_x;
            double e1y = by + chevron_half_w * perp_y;
            double e2x = bx - chevron_half_w * perp_x;
            double e2y = by - chevron_half_w * perp_y;

            // Dark under-stroke for contrast against the white needle.
            cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
            cairo_set_line_width(cr, 6.0);
            cairo_move_to(cr, e1x, e1y);
            cairo_line_to(cr, ax, ay);
            cairo_line_to(cr, e2x, e2y);
            cairo_stroke(cr);

            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            cairo_set_line_width(cr, 3.5);
            cairo_move_to(cr, e1x, e1y);
            cairo_line_to(cr, ax, ay);
            cairo_line_to(cr, e2x, e2y);
            cairo_stroke(cr);
        }
        cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
        cairo_set_line_join(cr, CAIRO_LINE_JOIN_MITER);
    }

    // Compact layout: draw the speed values inside the gauge area.
    // Fonts match the wide layout at full size and shrink with the gauge.
    if (data->driverCompactMode) {
        CompactGaugeLayout L = computeCompactGaugeLayout(width, height);

        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_text_extents_t te;

        // Right-aligned Total/Trip value, at the enlarged curTopSize
        // (RB-DRV-08): a fixed right anchor keeps the decimal point in
        // place as the digits change.
        auto drawValue = [&](const char* text, double baseline) {
            cairo_set_font_size(cr, L.curTopSize);
            cairo_text_extents(cr, text, &te);
            cairo_move_to(cr, L.rightAnchor - te.x_advance, baseline);
            cairo_show_text(cr, text);
        };
        // Row caption, written just to the right of the value column so the
        // values themselves stay in one unbroken vertical line.
        auto drawCaption = [&](const char* text, double baseline) {
            cairo_set_font_size(cr, L.labelSize);
            cairo_move_to(cr, L.rightAnchor + L.labelGap, baseline);
            cairo_show_text(cr, text);
        };
        // Right-aligned distance, at curTopSize (RB-DRV-08), on the same
        // baseline as its speed, to distanceAnchor -- the same X the
        // "Distance (metres)" caption right-aligns to (RB-DRV-02), so the
        // value's last digit and the caption's closing ")" share one
        // vertical edge.
        auto drawDistance = [&](const char* text, double baseline) {
            cairo_set_font_size(cr, L.curTopSize);
            cairo_text_extents(cr, text, &te);
            cairo_move_to(cr, L.distanceAnchor - te.x_advance, baseline);
            cairo_show_text(cr, text);
        };
        // Top-corner value: right-aligns to `anchor_x` (Current: bandOuterX;
        // Target: mirrored, left-aligned from bandTargetX), top-aligns with
        // L.bandTopY using the drawn text's own ink ascent (font-metric
        // ascent isn't pure geometry, so this can't be precomputed in
        // CompactGaugeLayout -- RB-DRV-08). Returns the baseline used, so
        // the caller can place the label directly beneath it.
        auto drawTopCorner = [&](const char* text, double anchor_x, bool right_align) -> double {
            cairo_set_font_size(cr, L.curTopSize);
            cairo_text_extents(cr, text, &te);
            double ascent = -te.y_bearing;
            double baseline = L.bandTopY + ascent;
            double x = right_align ? (anchor_x - te.x_advance) : anchor_x;
            cairo_move_to(cr, x, baseline);
            cairo_show_text(cr, text);
            return baseline;
        };
        auto drawTopCornerLabel = [&](const char* text, double anchor_x, bool right_align, double value_baseline) {
            cairo_set_font_size(cr, L.labelSize);
            cairo_text_extents_t lte;
            cairo_text_extents(cr, text, &lte);
            double x = right_align ? (anchor_x - lte.x_advance) : anchor_x;
            cairo_move_to(cr, x, value_baseline + L.labelSize + L.labelGap);
            cairo_show_text(cr, text);
        };

        // Current: top-right corner, right-aligned to the coloured band's
        // outer edge, top-aligned with the band's topmost point, "Current"
        // label underneath (RB-DRV-08).
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        {
            double baseline = drawTopCorner(gtk_label_get_text(data->currentSpeedLabel), L.bandOuterX, true);
            drawTopCornerLabel("Current", L.bandOuterX, true, baseline);
        }

        // Target: top-left corner, mirrored -- left-aligned to the band's
        // other edge, "Target" label underneath. Still the only row pulled
        // out of white -- it is the value being driven toward.
        cairo_set_source_rgb(cr, 1.0, 0.867, 0.0);  // #FFDD00
        {
            double baseline = drawTopCorner(gtk_label_get_text(data->targetSpeedLabel), L.bandTargetX, false);
            drawTopCornerLabel("Target", L.bandTargetX, false, baseline);
            // On a stage with more than one speed, the NEXT segment's target
            // goes under the caption in smaller type, so the co-pilot can call
            // the change before it arrives. Nothing is drawn on the last
            // segment or a single-speed stage -- an empty slot reads as "no
            // change coming", which is the truth.
            double next_kph = 0.0;
            if (nextSegmentTargetKph(*data->state, &next_kph)) {
                std::stringstream ns;
                ns << std::fixed << std::setprecision(1)
                   << (data->state->units ? next_kph / 1.60934 : next_kph);
                cairo_set_font_size(cr, L.labelSize);
                cairo_move_to(cr, L.bandTargetX,
                              baseline + 2 * (L.labelSize + L.labelGap));
                cairo_show_text(cr, ns.str().c_str());
            }
        }

        // {tot}: white, mirrored by the Total distance on the left. Both at
        // curTopSize, nudged up with extra line spacing (RB-DRV-08) so the
        // taller glyphs clear the ahead/behind box above them.
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        drawValue(gtk_label_get_text(data->totalSpeedLabel), L.totalBaseline);
        drawCaption("Total", L.totalBaseline);
        drawDistance(data->driverTotalDistText.c_str(), L.totalBaseline);

        // Trip speed and Trip distance share a colour so the eye groups them
        // as one reading, instead of scanning four undifferentiated white
        // numbers and working out which distance belongs to which speed.
        cairo_set_source_rgb(cr, 0.0, 1.0, 1.0);  // #00FFFF
        drawValue(gtk_label_get_text(data->tripSpeedLabel), L.tripBaseline);
        drawCaption("Trip", L.tripBaseline);
        drawDistance(data->driverTripDistText.c_str(), L.tripBaseline);

        // Column captions below each half of the panel. They carry the
        // units so the values above them do not have to repeat them on
        // every row -- and they follow the live unit, not a fixed string.
        {
            std::string dist_caption =
                distanceColumnCaption(data->driverTotalUnitText.c_str());
            std::string speed_caption = speedColumnCaption(data->state->units);

            cairo_set_font_size(cr, L.labelSize);
            cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);

            // Right-aligned to the same X the distance values themselves
            // right-align to (RB-DRV-01's L.distanceAnchor), so the
            // caption's closing ")" sits directly under the values' last
            // digit rather than centred under a right-aligned column.
            cairo_text_extents(cr, dist_caption.c_str(), &te);
            cairo_move_to(cr, L.distanceAnchor - te.x_advance, L.captionBaseline);
            cairo_show_text(cr, dist_caption.c_str());

            // Centred (nudged 15px right at the user's request) -- there is
            // no value column edge on this side to match.
            cairo_text_extents(cr, speed_caption.c_str(), &te);
            cairo_move_to(cr, (L.centerX + width) / 2 - te.x_advance / 2 + 15.0, L.captionBaseline);
            cairo_show_text(cr, speed_caption.c_str());
        }

        // fps left, cpu right, baseline in line with the bottom of the
        // ahead/behind digital readout box.
        double foot_baseline = L.footBaseline;
        double foot_size = L.footSize;
        cairo_set_font_size(cr, foot_size);
        cairo_set_source_rgb(cr, 0.7, 0.7, 0.7);
        cairo_move_to(cr, 15, foot_baseline);
        cairo_show_text(cr, gtk_label_get_text(data->updatesPerSecLabel));
        const char* cpu_text = gtk_label_get_text(data->cpuTempLabel);
        cairo_text_extents(cr, cpu_text, &te);
        cairo_move_to(cr, width - 15 - te.x_advance, foot_baseline);
        cairo_show_text(cr, cpu_text);

        // Single-display mode: rally clock hard top-right, bright white
        // (replaces the alarm panel's clock)
        if (data->singleDisplayMode && data->copilotRallyClockLabel) {
            double clock_size = std::max(20.0, 28 * L.fscale);
            cairo_set_font_size(cr, clock_size);
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            const char* clock_text = gtk_label_get_text(data->copilotRallyClockLabel);
            cairo_text_extents(cr, clock_text, &te);
            // Align the glyph ink (bearing + width), not the advance, so the
            // visible right edge sits flush with the panel edge
            cairo_move_to(cr, width - (te.x_bearing + te.width), clock_size);
            cairo_show_text(cr, clock_text);
        }
    }

    return FALSE;
}

void updateDriverDisplay(AppData* data) {
    auto current_poll = data->poller->getMostRecent();
    auto tenth_poll = data->poller->get10th();
    auto current_time_ms = getRallyTime_ms(*data->state);
    
    // Switch to compact layout (values drawn inside the gauge) when the
    // window is closer to 4:3 (e.g. 800x480) than wide-and-shallow 1280x400.
    // In single-display mode the gauge is embedded in the co-pilot window and
    // compact mode is forced on at startup, so skip the aspect-ratio toggle.
    if (!data->singleDisplayMode && data->driverWindow && data->driverSpeedsBox) {
        int win_w = 0, win_h = 0;
        gtk_window_get_size(GTK_WINDOW(data->driverWindow), &win_w, &win_h);
        bool compact = (win_h > 0) && (static_cast<double>(win_w) / win_h) < 2.2;
        if (compact != data->driverCompactMode) {
            data->driverCompactMode = compact;
            if (compact) {
                gtk_widget_hide(data->driverSpeedsBox);
            } else {
                gtk_widget_show_all(data->driverSpeedsBox);
            }
        }
    }
    
    // Current speed (from rolling average, then EMA-smoothed for display)
    double current_speed = calculateCurrentSpeed(*data->state, current_poll, tenth_poll);
    if (current_speed < 0) {
        data->smoothedSpeed = -1.0;
    } else if (data->smoothedSpeed < 0) {
        data->smoothedSpeed = current_speed;
    } else {
        constexpr double alpha = 0.02;
        data->smoothedSpeed = alpha * current_speed + (1.0 - alpha) * data->smoothedSpeed;
    }
    std::stringstream ss;
    if (data->smoothedSpeed < 0) {
        ss << "--.-";
    } else {
        ss << std::fixed << std::setprecision(1) << data->smoothedSpeed;
    }
    gtk_label_set_text(data->currentSpeedLabel, ss.str().c_str());
    
    // Trip average speed
    int64_t trip_count_diff = calculateDistanceCounts(*data->state, 
        current_poll.cntr1, current_poll.cntr2,
        data->state->trip_start_cntr1, data->state->trip_start_cntr2);
    double trip_speed = calculateAverageSpeed(*data->state,
        data->state->trip_start_time_ms, current_time_ms, trip_count_diff,
        data->state->trip_distance_adjust_cm);
    ss.str("");
    ss << std::fixed << std::setprecision(1) << trip_speed;
    gtk_label_set_text(data->tripSpeedLabel, ss.str().c_str());
    
    // Total average speed
    int64_t total_count_diff = calculateDistanceCounts(*data->state,
        current_poll.cntr1, current_poll.cntr2,
        data->state->total_start_cntr1, data->state->total_start_cntr2);
    double total_speed = calculateAverageSpeed(*data->state,
        data->state->total_start_time_ms, current_time_ms, total_count_diff,
        data->state->total_distance_adjust_cm);
    ss.str("");
    ss << std::fixed << std::setprecision(1) << total_speed;
    gtk_label_set_text(data->totalSpeedLabel, ss.str().c_str());

    // Total/Trip distance, reusing the count differences just computed for
    // the average speeds above -- no additional counter reads. Applies the
    // same manual correction the co-pilot applies (RB-NAV-03), from the
    // start: if the two panels ever disagreed about distance travelled, even
    // briefly, that would be worse than not having the correction at all.
    long total_dist_m = adjustedDistanceMeters(
        countsToCentimeters(total_count_diff, data->state->calibration),
        data->state->total_distance_adjust_cm);
    const char* total_unit = "m";
    std::string total_dist_str = formatDistanceAutoUnit(total_dist_m, &total_unit);
    data->driverTotalDistText = total_dist_str;
    data->driverTotalUnitText = total_unit;

    long trip_dist_m = adjustedDistanceMeters(
        countsToCentimeters(trip_count_diff, data->state->calibration),
        data->state->trip_distance_adjust_cm);
    const char* trip_unit = "m";
    std::string trip_dist_str = formatDistanceAutoUnit(trip_dist_m, &trip_unit);
    data->driverTripDistText = trip_dist_str;
    data->driverTripUnitText = trip_unit;

    // Target speed and ahead/behind
    if (data->state->segment_current_number >= 0 && 
        data->state->segment_current_number < static_cast<long>(data->state->segments.size())) {
        const Segment& seg = data->state->segments[data->state->segment_current_number];
        double target_kph = countsPerHourToKPH(seg.target_speed_counts_per_hour, data->state->calibration);
        if (data->state->units) {
            target_kph = target_kph * 0.621371;  // Convert to MPH
        }
        ss.str("");
        ss << std::fixed << std::setprecision(1) << target_kph;
        gtk_label_set_text(data->targetSpeedLabel, ss.str().c_str());
        gtk_label_set_text(data->gaugeTargetLabel, ss.str().c_str());
        // The coming change, in the same units as the value above it.
        double next_kph = 0.0;
        if (data->nextTargetSpeedLabel) {
            if (nextSegmentTargetKph(*data->state, &next_kph)) {
                std::stringstream ns;
                ns << std::fixed << std::setprecision(1)
                   << (data->state->units ? next_kph / 1.60934 : next_kph);
                gtk_label_set_text(data->nextTargetSpeedLabel, ns.str().c_str());
            } else {
                gtk_label_set_text(data->nextTargetSpeedLabel, "");
            }
        }
        
        // Ahead/behind - calculated from stage start accounting for all segment speeds
        int64_t total_count_diff_ab = calculateDistanceCounts(*data->state,
            current_poll.cntr1, current_poll.cntr2,
            data->state->total_start_cntr1, data->state->total_start_cntr2);
        double seconds = calculateAheadBehindFromStageStart(*data->state, current_time_ms, total_count_diff_ab);
        seconds += data->state->ahead_behind_zero_offset_ms / 1000.0;
        
        // Store for gauge
        data->aheadBehindSeconds = seconds;

        // Fraction of the current segment driven, for the needle's scale chevrons
        // and segment-end tick. "In segment" while between the segment start and end.
        {
            int64_t seg_count_diff = calculateDistanceCounts(*data->state,
                current_poll.cntr1, current_poll.cntr2,
                data->state->segment_start_cntr1, data->state->segment_start_cntr2);
            double seg_total = seg.distance_counts;
            if (seg_total > 0.0) {
                double frac = static_cast<double>(seg_count_diff) / seg_total;
                if (frac < 0.0) frac = 0.0;
                if (frac > 1.0) frac = 1.0;
                data->segmentProgress = frac;
                data->inSegment = (seg_count_diff >= 0 &&
                                   static_cast<double>(seg_count_diff) <= seg_total);
            } else {
                data->segmentProgress = 0.0;
                data->inSegment = false;
            }
        }
        
        // Format as mm:ss.s (not hh:mm:ss.ss)
        ss.str("");
        if (seconds >= 0) {
            ss << "+";
        } else {
            ss << "-";
        }
        double abs_seconds = std::abs(seconds);
        int total_sec = static_cast<int>(abs_seconds);
        int tenths = static_cast<int>((abs_seconds - total_sec) * 10);
        int mins = total_sec / 60;
        int secs = total_sec % 60;
        ss << std::setfill('0') << std::setw(2) << mins << ":"
           << std::setw(2) << secs << "." << tenths;
        gtk_label_set_text(data->aheadBehindLabel, ss.str().c_str());
        
        // Speed adjustment arrows (always driven by the arrow-based
        // calculation, regardless of tone mode -- the visual indicator is
        // unaffected by simple_tone_mode) and the ahead/behind tone
        // (driven by whichever algorithm simple_tone_mode selects).
        double stage_dist_m = countsToMeters(total_count_diff_ab, data->state->calibration);
        double total_stage_counts = 0.0;
        for (const auto& s : data->state->segments)
            total_stage_counts += s.distance_counts;
        bool past_stage_end = (static_cast<double>(total_count_diff_ab) >= total_stage_counts);

        ArrowToneResult arrow = computeArrowBasedTone(
            seconds, seg.target_speed_counts_per_hour, data->state->calibration,
            data->state->units, stage_dist_m, past_stage_end);

        if (arrow.num_arrows > 0) {
            ss.str("");
            const char* color = arrow.increase_speed ? "#00CC00" : "#EE0000";
            const char* glyph = arrow.increase_speed ? "↑" : "↓";
            ss << "<span foreground=\"" << color << "\">";
            for (int i = 0; i < arrow.num_arrows; i++) ss << glyph;
            ss << "</span>";
            gtk_label_set_markup(data->speedAdjustArrowsLabel, ss.str().c_str());
        } else {
            gtk_label_set_text(data->speedAdjustArrowsLabel, "");
        }

        if (data->toneGen) {
            if (!data->state->tone_enabled) {
                data->toneGen->setCadence(0, 0);
            } else if (data->state->simple_tone_mode) {
                SimpleToneResult simple = updateSimpleTone(
                    data->simpleToneState, seconds, stage_dist_m, past_stage_end);
                if (simple.active) {
                    ToneWaveform wave = simple.triangle_wave ? ToneWaveform::Triangle : ToneWaveform::Sine;
                    data->toneGen->setCadence(SIMPLE_TONE_SUSTAIN_MS, 0, simple.freq_hz, wave);
                } else {
                    data->toneGen->setCadence(0, 0);
                }
            } else {
                if (arrow.tone_active) {
                    data->toneGen->setCadence(arrow.tone_ms, arrow.silence_ms, arrow.freq_hz);
                } else {
                    data->toneGen->setCadence(0, 0);
                }
            }
        }

        // Redraw gauge
        gtk_widget_queue_draw(data->rallyGaugeDrawingArea);
        if (data->copilotGaugeArea)
            gtk_widget_queue_draw(data->copilotGaugeArea);
    } else {
        gtk_label_set_text(data->targetSpeedLabel, "--.-");
        gtk_label_set_text(data->gaugeTargetLabel, "--.-");
        if (data->nextTargetSpeedLabel) gtk_label_set_text(data->nextTargetSpeedLabel, "");
        gtk_label_set_text(data->aheadBehindLabel, "--:--.--");
        gtk_label_set_text(data->speedAdjustArrowsLabel, "");
        if (data->toneGen) data->toneGen->setCadence(0, 0);
        data->aheadBehindSeconds = 0.0;
        data->segmentProgress = 0.0;
        data->inSegment = false;
        if (data->rallyGaugeDrawingArea) {
            gtk_widget_queue_draw(data->rallyGaugeDrawingArea);
        }
        if (data->copilotGaugeArea) {
            gtk_widget_queue_draw(data->copilotGaugeArea);
        }
    }
    
    // Next segment info
    if (data->state->segment_current_number >= 0 && 
        data->state->segment_current_number < static_cast<long>(data->state->segments.size()) - 1) {
        const Segment& current_seg = data->state->segments[data->state->segment_current_number];
        int64_t seg_count_diff = calculateDistanceCounts(*data->state,
            current_poll.cntr1, current_poll.cntr2,
            data->state->segment_start_cntr1, data->state->segment_start_cntr2);
        
        double remaining_counts = current_seg.distance_counts - static_cast<double>(seg_count_diff);
        double remaining_m = countsToMeters(static_cast<int64_t>(remaining_counts), data->state->calibration);
        
        const Segment& next_seg = data->state->segments[data->state->segment_current_number + 1];
        double next_target = countsPerHourToKPH(next_seg.target_speed_counts_per_hour, data->state->calibration);
        if (data->state->units) {
            next_target = next_target * 0.621371;
        }
        
        if (current_speed > 0 && current_speed != -1) {
            double speed_m_per_s = current_speed;
            if (data->state->units) {
                speed_m_per_s = speed_m_per_s * 1.60934;
            }
            speed_m_per_s = speed_m_per_s / 3.6;
            double eta_seconds = remaining_m / speed_m_per_s;
            
            if (eta_seconds < 0) {
                ss.str("");
                ss << "Over by " << formatDuration(static_cast<int64_t>(-eta_seconds * 1000));
                gtk_label_set_text(data->nextSegLabel, ss.str().c_str());
            } else {
                ss.str("");
                ss << "next: " << std::fixed << std::setprecision(2) << next_target 
                   << " in " << static_cast<long>(remaining_m) << " m  ETA " << formatDuration(static_cast<int64_t>(eta_seconds * 1000));
                gtk_label_set_text(data->nextSegLabel, ss.str().c_str());
            }
        } else {
            ss.str("");
            ss << "next: " << std::fixed << std::setprecision(2) << next_target 
               << " in " << static_cast<long>(remaining_m) << " m  ETA --:--:--";
            gtk_label_set_text(data->nextSegLabel, ss.str().c_str());
        }
    } else {
        gtk_label_set_text(data->nextSegLabel, "");
    }
    
    // FPS counter and CPU temp (refreshed once per second)
    data->updateCount++;
    if (current_time_ms - data->lastUpdateCountTime_ms >= 1000) {
        ss.str("");
        ss << "fps: " << data->updateCount;
        gtk_label_set_text(data->updatesPerSecLabel, ss.str().c_str());
        gtk_label_set_text(data->cpuTempLabel, readCpuTemp().c_str());
        data->updateCount = 0;
        data->lastUpdateCountTime_ms = current_time_ms;
    }
    
    // Auto-start countdown overlay
    if (data->state->auto_start_rally_time_minutes > 0 && !data->autoStartTriggered) {
        struct tm epoch_tm = {};
        epoch_tm.tm_year = 120;
        epoch_tm.tm_mon = 0;
        epoch_tm.tm_mday = 1;
        int64_t epoch_ms = static_cast<int64_t>(mktime(&epoch_tm)) * 1000;
        int64_t target_ms = epoch_ms + 
            static_cast<int64_t>(data->state->auto_start_rally_time_minutes) * 60000;
        int64_t diff_ms = target_ms - current_time_ms;
        
        if (diff_ms > 0 && diff_ms <= 24LL * 3600 * 1000) {
            int total_secs = static_cast<int>(diff_ms / 1000);
            int h = total_secs / 3600;
            int m = (total_secs % 3600) / 60;
            int s = total_secs % 60;
            char buf[32];
            snprintf(buf, sizeof(buf), "T- %02d:%02d:%02d", h, m, s);
            gtk_label_set_text(data->countdownLabel, buf);
            // Which kind of autostart is pending, so the crew can see at a
            // glance whether rolling before the minute is going to count.
            gtk_label_set_text(data->earlyDepartureLabel,
                data->state->auto_start_early_departure ? "Early Departure : ENABLED"
                                                        : "Early Departure: DISABLED");
            if (data->countdownContainer) gtk_widget_show(data->countdownContainer);
        } else if (diff_ms <= 0 && diff_ms > -2000) {
            if (data->countdownContainer) gtk_widget_hide(data->countdownContainer);
            data->autoStartTriggered = true;
            // Branches internally on auto_start_early_departure: an early
            // departure zeroed its distance when it was armed, so only the
            // clock starts here.
            performAutoStart(data);
        } else {
            if (data->countdownContainer) gtk_widget_hide(data->countdownContainer);
        }
    } else {
        // No autostart pending at all: the whole block goes, Early Departure
        // line included.
        if (data->countdownContainer && gtk_widget_get_visible(data->countdownContainer)) {
            gtk_widget_hide(data->countdownContainer);
        }
    }
}

// Apply CSS styling for large fonts
static void applyDriverCSS(G_GNUC_UNUSED GtkWidget* widget) {
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "window, .background { background-color: #000000; }"
        "label { color: #FFFFFF; font-weight: bold; }"
        "button { background-color: #333333; color: #FFFFFF; font-weight: bold; }"
        ".speed-header { font-size: 28px; }"
        ".speed-value-next { font-size: 34px; font-family: monospace; color: #FFDD00; }"
        ".speed-value { font-size: 64px; font-family: monospace; }"
        ".speed-value-xl { font-size: 80px; font-family: monospace; }"
        ".speed-value-target { font-size: 45px; font-family: monospace; }"
        ".target-info { font-size: 22px; }"
        ".ahead-behind { font-size: 28px; font-family: monospace; }"
        ".next-info { font-size: 18px; }"
        ".footer-info { font-size: 14px; }"
        ".speed-arrows { font-size: 28px; }",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

GtkWidget* createDriverWindow(AppData* data) {
    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Driver Display");
    gtk_window_set_default_size(GTK_WINDOW(window), 1280, 400);
    
    applyDriverCSS(window);
    
    // Top-level overlay for countdown display
    data->countdownOverlay = gtk_overlay_new();
    gtk_container_add(GTK_CONTAINER(window), data->countdownOverlay);
    
    GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_set_border_width(GTK_CONTAINER(mainBox), 2);
    gtk_container_add(GTK_CONTAINER(data->countdownOverlay), mainBox);
    
    // Main content: left side speeds + footer, right side gauge (full height)
    GtkWidget* contentBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(mainBox), contentBox, TRUE, TRUE, 0);
    
    // Left side: vertical box holding speed columns on top, footer on bottom (~40% width)
    GtkWidget* speedsBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(speedsBox, 580, -1);
    gtk_box_pack_start(GTK_BOX(contentBox), speedsBox, FALSE, TRUE, 0);
    data->driverSpeedsBox = speedsBox;
    
    // Two-column speed display row
    GtkWidget* speedColsBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_pack_start(GTK_BOX(speedsBox), speedColsBox, TRUE, TRUE, 0);
    
    // Left column: Current speed + arrows, Target speed
    GtkWidget* leftCol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(speedColsBox), leftCol, TRUE, TRUE, 0);
    
    // Current header with arrows beside it
    GtkWidget* currentHeaderRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_halign(currentHeaderRow, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(leftCol), currentHeaderRow, FALSE, FALSE, 0);
    
    GtkWidget* currentHeader = gtk_label_new("Current");
    gtk_style_context_add_class(gtk_widget_get_style_context(currentHeader), "speed-header");
    gtk_box_pack_start(GTK_BOX(currentHeaderRow), currentHeader, FALSE, FALSE, 0);
    
    data->speedAdjustArrowsLabel = GTK_LABEL(gtk_label_new(""));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->speedAdjustArrowsLabel)), "speed-arrows");
    gtk_widget_set_valign(GTK_WIDGET(data->speedAdjustArrowsLabel), GTK_ALIGN_END);
    gtk_label_set_width_chars(data->speedAdjustArrowsLabel, 3);
    gtk_box_pack_start(GTK_BOX(currentHeaderRow), GTK_WIDGET(data->speedAdjustArrowsLabel), FALSE, FALSE, 0);
    
    // Current speed value (fixed width, right-aligned so decimal stays put)
    data->currentSpeedLabel = GTK_LABEL(gtk_label_new("--.-"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->currentSpeedLabel)), "speed-value-xl");
    gtk_label_set_width_chars(data->currentSpeedLabel, 6);
    gtk_label_set_xalign(data->currentSpeedLabel, 1.0);
    gtk_widget_set_halign(GTK_WIDGET(data->currentSpeedLabel), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(GTK_WIDGET(data->currentSpeedLabel), GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(leftCol), GTK_WIDGET(data->currentSpeedLabel), TRUE, TRUE, 0);
    
    // Target speed
    GtkWidget* targetHeader = gtk_label_new("Target");
    gtk_style_context_add_class(gtk_widget_get_style_context(targetHeader), "speed-header");
    gtk_widget_set_halign(targetHeader, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(leftCol), targetHeader, FALSE, FALSE, 0);
    
    data->targetSpeedLabel = GTK_LABEL(gtk_label_new("--.-"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->targetSpeedLabel)), "speed-value-target");
    gtk_label_set_width_chars(data->targetSpeedLabel, 6);
    gtk_label_set_xalign(data->targetSpeedLabel, 1.0);
    gtk_widget_set_halign(GTK_WIDGET(data->targetSpeedLabel), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(GTK_WIDGET(data->targetSpeedLabel), GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(leftCol), GTK_WIDGET(data->targetSpeedLabel), TRUE, TRUE, 0);

    // Next segment's target, smaller, directly under it. Blank -- not hidden --
    // when there is no next segment, so the Target block keeps a constant
    // height and the rows below it do not shift as segments advance.
    data->nextTargetSpeedLabel = GTK_LABEL(gtk_label_new(""));
    gtk_style_context_add_class(
        gtk_widget_get_style_context(GTK_WIDGET(data->nextTargetSpeedLabel)), "speed-value-next");
    gtk_label_set_width_chars(data->nextTargetSpeedLabel, 6);
    gtk_label_set_xalign(data->nextTargetSpeedLabel, 1.0);
    gtk_widget_set_halign(GTK_WIDGET(data->nextTargetSpeedLabel), GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(leftCol), GTK_WIDGET(data->nextTargetSpeedLabel), FALSE, FALSE, 0);

    // Right column: Total + Trip (vertically aligned)
    GtkWidget* rightCol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(speedColsBox), rightCol, TRUE, TRUE, 0);
    
    GtkWidget* totalHeader = gtk_label_new("Total");
    gtk_style_context_add_class(gtk_widget_get_style_context(totalHeader), "speed-header");
    gtk_widget_set_halign(totalHeader, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(rightCol), totalHeader, FALSE, FALSE, 0);
    
    data->totalSpeedLabel = GTK_LABEL(gtk_label_new("--.-"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->totalSpeedLabel)), "speed-value");
    gtk_label_set_width_chars(data->totalSpeedLabel, 6);
    gtk_label_set_xalign(data->totalSpeedLabel, 1.0);
    gtk_widget_set_halign(GTK_WIDGET(data->totalSpeedLabel), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(GTK_WIDGET(data->totalSpeedLabel), GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(rightCol), GTK_WIDGET(data->totalSpeedLabel), TRUE, TRUE, 0);
    
    GtkWidget* tripHeader = gtk_label_new("Trip");
    gtk_style_context_add_class(gtk_widget_get_style_context(tripHeader), "speed-header");
    gtk_widget_set_halign(tripHeader, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(rightCol), tripHeader, FALSE, FALSE, 0);
    
    data->tripSpeedLabel = GTK_LABEL(gtk_label_new("--.-"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->tripSpeedLabel)), "speed-value");
    gtk_label_set_width_chars(data->tripSpeedLabel, 6);
    gtk_label_set_xalign(data->tripSpeedLabel, 1.0);
    gtk_widget_set_halign(GTK_WIDGET(data->tripSpeedLabel), GTK_ALIGN_CENTER);
    gtk_widget_set_valign(GTK_WIDGET(data->tripSpeedLabel), GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(rightCol), GTK_WIDGET(data->tripSpeedLabel), TRUE, TRUE, 0);

    // Total/Trip distance: data only, not yet drawn anywhere. RB-DRV-01
    // places these in the compact-mode gauge per the design mockup.
    // Plain strings, not GtkLabels: these are text carriers read back by the
    // compact draw path, never packed into a container. As unparented widgets
    // their floating references were never sunk or released and they were
    // never destroyed.
    data->driverTotalDistText = "0";
    data->driverTotalUnitText = "m";
    data->driverTripDistText  = "0";
    data->driverTripUnitText  = "m";
    
    // Footer row at bottom of LEFT side only (under speeds)
    GtkWidget* footerBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_end(GTK_BOX(speedsBox), footerBox, FALSE, FALSE, 5);
    
    data->updatesPerSecLabel = GTK_LABEL(gtk_label_new("fps: 0"));
    data->cpuTempLabel = GTK_LABEL(gtk_label_new(readCpuTemp().c_str()));
    data->nextSegLabel = GTK_LABEL(gtk_label_new(""));
    
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->updatesPerSecLabel)), "footer-info");
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->cpuTempLabel)), "footer-info");
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->nextSegLabel)), "next-info");
    
    gtk_box_pack_start(GTK_BOX(footerBox), GTK_WIDGET(data->updatesPerSecLabel), FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(footerBox), GTK_WIDGET(data->cpuTempLabel), FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(footerBox), GTK_WIDGET(data->nextSegLabel), TRUE, TRUE, 0);
    
    // Right side: Rally gauge fills full height. The unit (KPH/MPH) toggle lives on
    // the co-pilot Date/Time setup screen, not here.
    GtkWidget* gaugeOverlay = gtk_overlay_new();
    gtk_widget_set_vexpand(gaugeOverlay, TRUE);
    gtk_widget_set_hexpand(gaugeOverlay, TRUE);
    gtk_box_pack_end(GTK_BOX(contentBox), gaugeOverlay, TRUE, TRUE, 0);
    
    data->rallyGaugeDrawingArea = gtk_drawing_area_new();
    gtk_widget_set_hexpand(data->rallyGaugeDrawingArea, TRUE);
    gtk_widget_set_vexpand(data->rallyGaugeDrawingArea, TRUE);
    g_signal_connect(data->rallyGaugeDrawingArea, "draw", G_CALLBACK(on_gauge_draw), data);
    gtk_container_add(GTK_CONTAINER(gaugeOverlay), data->rallyGaugeDrawingArea);
    
    // Units label (hidden, kept for update logic compatibility)
    data->unitsLabel = GTK_LABEL(gtk_label_new(data->state->units ? "(MPH)" : "(KPH)"));
    
    // Hidden labels still needed by update logic
    data->aheadBehindLabel = GTK_LABEL(gtk_label_new(""));
    data->gaugeTargetLabel = GTK_LABEL(gtk_label_new(""));
    
    // Countdown overlay label in an event box for background styling
    data->countdownLabel = GTK_LABEL(gtk_label_new("T- 00:00:00"));
    gtk_widget_set_margin_start(GTK_WIDGET(data->countdownLabel), 30);
    gtk_widget_set_margin_end(GTK_WIDGET(data->countdownLabel), 30);
    gtk_widget_set_margin_top(GTK_WIDGET(data->countdownLabel), 15);
    gtk_widget_set_margin_bottom(GTK_WIDGET(data->countdownLabel), 15);
    
    GtkWidget* countdownBox = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(countdownBox), GTK_WIDGET(data->countdownLabel));
    gtk_widget_set_halign(countdownBox, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(countdownBox, GTK_ALIGN_CENTER);
    
    GtkCssProvider* cdProvider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(cdProvider,
        ".countdown-box { border: 4px solid white; background-color: #000000; }"
        ".countdown-label { font-size: 60px; font-family: monospace; color: white; font-weight: bold; }"
        ".early-departure-label { font-size: 22px; color: white; font-weight: bold; }",
        -1, nullptr);
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(countdownBox),
        GTK_STYLE_PROVIDER(cdProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 50);
    gtk_style_context_add_class(gtk_widget_get_style_context(countdownBox), "countdown-box");
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(GTK_WIDGET(data->countdownLabel)),
        GTK_STYLE_PROVIDER(cdProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 50);
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->countdownLabel)), "countdown-label");
    g_object_unref(cdProvider);
    
    // "Early Departure" line, directly beneath the box.
    data->earlyDepartureLabel = GTK_LABEL(gtk_label_new(""));
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(GTK_WIDGET(data->earlyDepartureLabel)),
        GTK_STYLE_PROVIDER(cdProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 50);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(GTK_WIDGET(data->earlyDepartureLabel)), "early-departure-label");

    GtkWidget* countdownStack = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_halign(countdownStack, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(countdownStack, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(countdownStack), countdownBox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(countdownStack), GTK_WIDGET(data->earlyDepartureLabel), FALSE, FALSE, 0);
    data->countdownContainer = countdownStack;

    gtk_overlay_add_overlay(GTK_OVERLAY(data->countdownOverlay), countdownStack);
    gtk_widget_show_all(countdownStack);
    gtk_widget_hide(countdownStack);
    
    return window;
}
