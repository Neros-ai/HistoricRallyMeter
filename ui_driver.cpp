#include "ui_driver.h"
#include "home_calc.h"
#include "stage_rows.h"
#include "calculations.h"
#include "rally_types.h"
#include "sector.h"
#include "rally_state.h"
#include "counter_poller.h"
#include "tone_generator.h"
#include "callbacks.h"
#include "simple_tone.h"
#include "arrow_tone.h"
#include <iomanip>
#include <sstream>
#include <algorithm>
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
    // Compact geometry comes from computeCompactGaugeLayout() rather than
    // being recomputed here: the hub's position now differs between the
    // driver display and the co-pilot's embedded panel, and two copies of
    // that rule would let the dial and the text it is laid out around drift
    // apart.
    const bool embeddedGauge = data->singleDisplayMode;
    CompactGaugeLayout L{};
    if (data->driverCompactMode) {
        L = computeCompactGaugeLayout(width, height, embeddedGauge);
    }
    double radius = data->driverCompactMode
        ? L.radius
        : (std::min(width / 2, height) - 25) * 0.8;
    double centerX = data->driverCompactMode ? L.centerX : width - radius - 20;
    double centerY = data->driverCompactMode ? L.centerY : (height + radius) / 2;

    // Every quantity below is a pure function of this one reading,
    // recomputed fresh every frame -- there is no discrete "scale" to
    // debounce or switch between any more.
    double seconds = data->aheadBehindSeconds;
    double max_val = gaugeEffectiveMaxSeconds(seconds, data->state->gauge_range_s);
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
    double fscale = data->driverCompactMode
        ? L.fscale
        : std::min(1.0, radius / REF_RADIUS);

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
    bool labels_visible = gaugeTickLabelsVisibleInZone(zone);
    int tick_count = static_cast<int>(max_val);
    // Fine ticks on the short ranges, while the sweep is still within a second
    // of the range: every tenth of a second on +-1 s, every two tenths on +-2 s and +-3 s.
    // Shorter than the second ticks; the half second a little longer.
    const int range = data->state->gauge_range_s;
    const int step = (max_val >= range + 1.0) ? 0 : (range == 1) ? 1 : 2;
    if (step) {
        for (int k = -39; k <= 39; k++) {
            if (k % 10 == 0 || k % step != 0) continue;
            const double h = k / 10.0;
            if (std::abs(h) > max_val) continue;
            const double inner = (std::abs(k) % 5 == 0) ? radius - 12 : radius - 2;
            double angle = M_PI + M_PI / 2 + (h / max_val) * (M_PI / 2);
            cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
            cairo_set_line_width(cr, NEEDLE_HALF_WIDTH * 2 * 0.6);
            cairo_move_to(cr, centerX + inner * cos(angle), centerY + inner * sin(angle));
            cairo_line_to(cr, centerX + (radius + 8) * cos(angle), centerY + (radius + 8) * sin(angle));
            cairo_stroke(cr);
        }
    }
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
    // RB-DRV-19: no needle and an empty error box outside a timed stretch (italian navi rows, between stages).
    const bool hideNeedle = data->driverCompactMode &&
        (data->driverPage == AppData::DP_ITALIAN_NAVI || data->driverPage == AppData::DP_GREEK_IDLE);
    if (hideNeedle) digital[0] = '\0';

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

    // Drawn under the readout so a sweeping needle does not cover the digits.
    // When the needle is pegged at either end it is drawn again, after the
    // stage tag and the error box, so those boxes do not hide it.
    auto drawNeedleBar = [&]() {
        cairo_set_source_rgb(cr, 0.9, 0.9, 0.9);
        cairo_move_to(cr, tip_x + needle.halfWidth * perp_x, tip_y + needle.halfWidth * perp_y);
        cairo_line_to(cr, tip_x - needle.halfWidth * perp_x, tip_y - needle.halfWidth * perp_y);
        cairo_line_to(cr, centerX - needle.halfWidth * perp_x, centerY - needle.halfWidth * perp_y);
        cairo_line_to(cr, centerX + needle.halfWidth * perp_x, centerY + needle.halfWidth * perp_y);
        cairo_close_path(cr);
        cairo_fill(cr);
    };
    if (!hideNeedle) drawNeedleBar();

    // Hub: a plain filled disc in the needle's own colour. The old ring and
    // inner dot were decoration on the one part of the dial that carries no
    // reading -- and in compact mode the readout box covers it anyway.
    if (!data->driverCompactMode) {   // compact: the readout box covers the hub
        cairo_arc(cr, centerX, centerY, 8, 0, 2 * M_PI);
        cairo_fill(cr);
    }

    // Digital ahead/behind readout. Drawn after the needle and hub so that in
    // compact mode it paints the hub out: the hub carries no information, and
    // this readout is the one thing the driver looks at, so it takes the
    // centre of the dial. The wide layout keeps the original smaller box
    // below the hub, where there is nothing to cover.
    // The error box's rectangle, kept for the pc / pm layout (RB-DRV-18), which sets its
    // logo to the left of the box and the countdown box to the right.
    double abBoxX = 0.0, abBoxY = 0.0, abBoxW = 0.0;
    [[maybe_unused]] double abBoxH = 0.0;
    {
        double digital_size = data->driverCompactMode ? L.boxFontSize : 22.0;
        // Bold monospace, the same face as the rest of the box.
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
            box_height = L.boxHeight;   // Nero2.0: 64 px at 50 px digits
            double box_center_y = L.boxY + L.boxHeight / 2.0;
            box_y = box_center_y - box_height / 2.0;
            if (data->driverPage != AppData::DP_CLASSIC) {
                // RB-DRV-18/19: the logo box, this box and the right-hand box form one row,
                // tops level, edges touching (16 px wider than before). The row was 80 px,
                // which started 8 px above the hub. A pinned needle is a 10 px bar through
                // the hub, so those boxes stuck up past it. Shorten from the top until the
                // bar lies on the top edge. The bottom stays where it was.
                box_width += 16.0 * L.fscale;
                box_height = 80.0 * L.fscale;
                const double needleTop = centerY - needle.halfWidth;
                if (box_y < needleTop) {
                    box_height -= needleTop - box_y;
                    box_y = needleTop;
                }
            }
            border = 3.0;
        } else {
            box_width  = std::max(130.0, dext.x_advance + 16);
            box_height = 36.0;
            box_y      = centerY + 18;
            border     = 2.0;
        }
        double box_x = centerX - box_width / 2;
        abBoxX = box_x; abBoxY = box_y; abBoxW = box_width; abBoxH = box_height;

        // The outline is centred on the path, so it spills half its width outside the rectangle.
        // In the compact strip (RB-DRV-19) the box must be exactly as tall as its neighbours, so the
        // stroke is drawn inside the rectangle instead.
        const double inset = (data->driverCompactMode && data->driverPage != AppData::DP_CLASSIC) ? border / 2.0 : 0.0;
        cairo_set_source_rgb(cr, 0.0, 0.0, 0.0);
        cairo_rectangle(cr, box_x, box_y, box_width, box_height);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
        cairo_set_line_width(cr, border);
        cairo_rectangle(cr, box_x + inset, box_y + inset, box_width - 2 * inset, box_height - 2 * inset);
        cairo_stroke(cr);

        if (data->driverCompactMode) {
            // RB-DRV-08: centre the "." on the box's horizontal centre (not
            // the whole string, which reads off-centre because of the
            // leading sign), and centre the digits vertically using the
            // glyphs' actual ink bounds rather than a hand-picked offset --
            // canvas/cairo baseline positioning is a font-metric
            // approximation that reads slightly off for monospace.
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
            cairo_move_to(cr, centerX - dext.x_advance / 2.0,
                              box_y + box_height / 2 + dext.height / 2 - 2);
            cairo_show_text(cr, digital);
        }
    }

    // Scale chevrons + segment-end tick along the needle.
    // One chevron; it carries the countdown along the needle (RB-DRV-19). The zone is shown by the arc colour only.
    // The topmost chevron starts 20% out from the hub; while within a segment the
    // whole group slides toward the segment-end tick (20% from the tip) in
    // proportion to how much of the segment has been driven. Outside a segment the
    // chevrons stay at the start position and the tick is hidden.
    auto drawChevrons = [&]() {
        // One chevron only; it carries the countdown (it no longer shows the zone).
        const int num_chevrons = 1;

        const double f_start = 0.20;  // topmost chevron start, fraction from hub
        const double f_tick  = 0.80;  // segment-end tick, 20% from the tip
        // RB-DRV-18: inside an italian pc / pm the chevrons wait at the start and slide to
        // the end tick over the last 5 s to the line, arriving as the countdown reads 0.00.
        const bool trialArrow = data->driverTrial.active;
        double progress = trialArrow ? data->driverTrial.arrowProgress
                                     : (data->inSegment ? data->segmentProgress : 0.0);
        if (progress < 0.0) progress = 0.0;
        if (progress > 1.0) progress = 1.0;
        // The tip's outline reaches 3 px past the apex, so stop the apex that far short: the tip then
        // lands on the end bar as the countdown reads 0, when the beep sounds.
        const double f_arrive = f_tick - 3.0 / needle_length;
        double f_top = f_start + progress * (f_arrive - f_start);

        const double chevron_half_w = 18.0;  // 36px wide
        const double chevron_depth  = 14.0;  // along the needle
        const double spacing_px     = 16.0;  // between stacked chevrons
        double spacing_f = spacing_px / needle_length;

        // Segment-end tick: 48px perpendicular line, only while within a segment.
        if (trialArrow || data->inSegment) {   // pc / pm: the end tick is there from the start, the chevrons arrive in the last 5 s
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
        // The chevrons slide up the needle toward the segment-end tick as the
        // segment is driven, on every layout (RB-DRV-17: RB-DRV-12 had dropped
        // them from the compact gauge by mistake).
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
    };
    if (!hideNeedle) drawChevrons();

    // Compact layout: draw the speed values inside the gauge area.
    // Fonts match the wide layout at full size and shrink with the gauge.
    if (data->driverCompactMode) {
        cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
        // The big figures (target, current speed, stage number, Total and Trip distances) use
        // bold monospace; the rest stays regular weight.
        auto bigFace = [&]() { cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD); };
        auto smallFace = [&]() { cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL); };
        cairo_text_extents_t te;

        // Right-aligned Total/Trip value, at the enlarged curTopSize
        // (RB-DRV-08): a fixed right anchor keeps the decimal point in
        // place as the digits change.
        [[maybe_unused]] auto drawValue = [&](const char* text, double baseline) {
            bigFace();
            cairo_set_font_size(cr, L.valSize);
            cairo_text_extents(cr, text, &te);
            cairo_move_to(cr, L.rightAnchor - te.x_advance, baseline);
            cairo_show_text(cr, text);
            smallFace();
        };
        // Row caption, written just to the right of the value column so the
        // values themselves stay in one unbroken vertical line.
        [[maybe_unused]] auto drawCaption = [&](const char* text, double baseline) {
            cairo_set_font_size(cr, L.rowCaptionSize);
            cairo_move_to(cr, L.rightAnchor + L.labelGap, baseline);
            cairo_show_text(cr, text);
        };
        // Right-aligned distance, at curTopSize (RB-DRV-08), on the same
        // baseline as its speed, to distanceAnchor -- the same X the
        // "Distance (metres)" caption right-aligns to (RB-DRV-02), so the
        // value's last digit and the caption's closing ")" share one
        // vertical edge.
        [[maybe_unused]] auto drawDistance = [&](const char* text, double baseline) {
            bigFace();
            cairo_set_font_size(cr, L.valSize);
            cairo_text_extents(cr, text, &te);
            cairo_move_to(cr, L.distanceAnchor - te.x_advance, baseline);
            cairo_show_text(cr, text);
            smallFace();
        };
        // Top-corner value. Both edges are the character cells, not the ink:
        // a right-hand figure ends on the anchor by its advance, a left-hand
        // figure starts on it. A narrow 1 then stays in its column. Top-aligns
        // with L.bandTopY using the drawn text's own ink ascent (font-metric
        // ascent isn't pure geometry, so this can't be precomputed in
        // CompactGaugeLayout -- RB-DRV-08). Returns the baseline used, so the
        // caller can place the label beneath it.
        auto drawTopCorner = [&](const char* text, double anchor_x, bool right_align) -> double {
            bigFace();
            cairo_set_font_size(cr, L.curTopSize);
            cairo_text_extents(cr, text, &te);
            double ascent = -te.y_bearing;
            double baseline = L.bandTopY + ascent;
            double x = right_align ? (anchor_x - te.x_advance) : anchor_x;
            cairo_move_to(cr, x, baseline);
            cairo_show_text(cr, text);
            smallFace();
            return baseline;
        };
        // Captions sit close under their figures: a fifth of the layout's legend drop.
        const double capDrop = L.legendDrop * 0.2;
        auto drawTopCornerLabel = [&](const char* text, double anchor_x, bool right_align, double value_baseline) {
            cairo_set_font_size(cr, L.labelSize);
            cairo_text_extents_t lte;
            cairo_text_extents(cr, text, &lte);
            double x = right_align ? (anchor_x - lte.x_advance) : anchor_x;
            cairo_move_to(cr, x, value_baseline + L.labelSize + L.labelGap + capDrop);
            cairo_show_text(cr, text);
        };

        // RB-DRV-19: the four compact pages -- italian pc / pm, italian navi, Greek in a stage,
        // Greek between stages -- share one set of sizes and positions. Everything is placed from
        // the error box's rectangle. Mockups: tools/layout-preview/nero2/driver-*.html.
        {
            const int page = data->driverPage;
            const DriverTrialView& v = data->driverTrial;
            const double fs = L.fscale;
            const bool italianFig = (page == AppData::DP_ITALIAN_TRIAL || page == AppData::DP_ITALIAN_NAVI);
            const bool mph = data->state->units && !italianFig;
            const char* speedUnit = mph ? "Mph" : "Kph";

            // ---- top right: present speed. The arc's top is near the hub, so this
            // stack is not tied to bandOuterX (the arc's right end). Ink ends a few pixels in.
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            {
                const double topRightInk = width - 4.0 * fs;
                const std::string sp = italianFig ? data->driverTrialSpeedText
                                                  : std::string(gtk_label_get_text(data->currentSpeedLabel));
                double baseline = drawTopCorner(sp.c_str(), topRightInk, true);
                drawTopCornerLabel(speedUnit, topRightInk, true, baseline);
            }

            // ---- top left: target (stages), or the speed to arrive at the next stage (between stages).
            // The arc's top is near the hub, so this stack is not tied to bandTargetX
            // (the arc's left end, down by the error box). Ink starts a few pixels in.
            cairo_set_source_rgb(cr, 1.0, 0.867, 0.0);  // #FFDD00
            {
                const double topLeftInk = 4.0 * fs;
                std::string big, cap = "Target";
                if (italianFig) big = data->driverTrialTargetText;
                else if (page == AppData::DP_GREEK_STAGE) big = gtk_label_get_text(data->targetSpeedLabel);
                else {
                    cap = mph ? "Arrival (mph)" : "Arrival (kph)";
                    if (data->driverArrivalKph < 0) big = "--";
                    else {
                        char b[16];
                        snprintf(b, sizeof(b), "%ld", mph ? std::lround(data->driverArrivalKph * 0.621371)
                                                          : static_cast<long>(data->driverArrivalKph));
                        big = b;
                    }
                }
                double baseline = drawTopCorner(big.c_str(), topLeftInk, false);
                drawTopCornerLabel(cap.c_str(), topLeftInk, false, baseline);
                // Arrival stays on the between-stages page (the figure above).
                // A Greek stage shows the target only, even while the navigator
                // still has Stage arrival.
                if (italianFig) {   // the countdown to the line, as on Home, under the target
                    bool cd_ok = false;
                    const double to_line = (page == AppData::DP_ITALIAN_TRIAL) ? v.toLineS
                        : sectorSecondsToLine(*data->state, getRallyTime_ms(*data->state), &cd_ok);
                    const std::string dur = (page == AppData::DP_ITALIAN_TRIAL || cd_ok) ? sectorSecondsWholeText(to_line) : std::string();
                    bigFace();
                    const double durSize = L.curTopSize * 0.64;
                    cairo_set_font_size(cr, durSize);
                    cairo_text_extents(cr, dur.c_str(), &te);
                    cairo_move_to(cr, topLeftInk,
                                  baseline + L.labelSize + L.labelGap + capDrop + durSize * 1.05);
                    cairo_show_text(cr, dur.c_str());
                    smallFace();
                }
            }

            // ---- the two figure rows, as the Greek page: trip above total; averages left, distances right
            const double lcx = abBoxX + abBoxW / 2.0;
            const double avgAnchor = abBoxX + 8.0 * fs;          // the original error box's left edge
            const double distAnchor = lcx + 260.0 * fs;           // 660 of 800
            const double y1 = abBoxY - 93.0 * fs, y2 = abBoxY - 27.0 * fs;
            auto drawAt = [&](const std::string& text, double anchor, double baseline) {
                bigFace();
                cairo_set_font_size(cr, L.valSize);
                cairo_text_extents(cr, text.c_str(), &te);
                cairo_move_to(cr, anchor - te.x_advance, baseline);
                cairo_show_text(cr, text.c_str());
                smallFace();
            };
            auto distText = [&](double metres) {
                return formatDistance(metres, data->state->distance_km);
            };
            // Italian speeds are km/h even when the box is set to mph. A label that is
            // not a number ("--.-" before the first reading) is drawn as it is.
            auto avgKphText = [&](const char* label) {
                if (!label || label[0] < '0' || label[0] > '9') return std::string(label ? label : "");
                const double x = atof(label);
                return trialSpeedText(data->state->units ? x / 0.621371 : x);
            };
            // A pc / pm keeps its own average over the tube. A navi row shows the live
            // Total average and the live Trip average, the same figures Greek recomputes
            // every tick. It used to put the Trip average in the Total slot and draw no
            // Trip average, so Total never moved.
            const char* totalLabel = gtk_label_get_text(data->totalSpeedLabel);
            const std::string totalAvg = (page == AppData::DP_ITALIAN_TRIAL)
                ? (v.avgValid ? trialSpeedText(v.avgKph) : std::string("--.--"))
                : (page == AppData::DP_ITALIAN_NAVI ? avgKphText(totalLabel)
                                                    : std::string(totalLabel));
            cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
            drawAt(totalAvg, avgAnchor, y2);
            drawAt(distText(page == AppData::DP_ITALIAN_TRIAL ? v.sectorTotalM : data->driverTotalM), distAnchor, y2);
            std::string tripAvg;
            bool haveTripAvg = false;
            if (page != AppData::DP_ITALIAN_TRIAL) {   // a pc / pm has no trip row
                cairo_set_source_rgb(cr, 0.0, 1.0, 1.0);
                if (page == AppData::DP_GREEK_STAGE) {   // the substage's own average, to match the time error
                    char sb[24];
                    const double sp = data->state->units ? data->driverSubAvgKph * 0.621371 : data->driverSubAvgKph;
                    if (data->driverSubAvgValid) snprintf(sb, sizeof(sb), "%.1f", sp); else snprintf(sb, sizeof(sb), "--.-");
                    tripAvg = sb;
                    haveTripAvg = true;
                    drawAt(tripAvg, avgAnchor, y1);
                } else {
                    const char* raw = gtk_label_get_text(data->tripSpeedLabel);
                    tripAvg = (page == AppData::DP_ITALIAN_NAVI) ? avgKphText(raw) : std::string(raw);
                    haveTripAvg = true;
                    drawAt(tripAvg, avgAnchor, y1);
                }
                drawAt(distText(data->driverTripM), distAnchor, y1);
            }
            // Left edge of the character cells of a figure on avgAnchor.
            // The ink of a 1 sits inside that cell, so the caption does not
            // step sideways as the digits change.
            auto avgLeftInk = [&](const std::string& text) {
                bigFace();
                cairo_set_font_size(cr, L.valSize);
                cairo_text_extents(cr, text.c_str(), &te);
                return avgAnchor - te.x_advance;
            };
            double numberLeft = avgLeftInk(totalAvg);
            if (haveTripAvg) numberLeft = std::min(numberLeft, avgLeftInk(tripAvg));

            // ---- legends: "avg / kph" left of the average, "Trip / km / Total" right of the distances
            {
                cairo_set_font_size(cr, 18.0 * fs);
                const char* unitLow = mph ? "mph" : "kph";
                const char* distUnit = data->state->distance_km ? "km" : "m";
                cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
                // Figures stay on avgAnchor. A long one pushes this caption left.
                const double legendRight = avgLegendRight(lcx - 288.0 * fs, numberLeft, 6.0 * fs);
                if (page == AppData::DP_ITALIAN_TRIAL) {   // a pc / pm average is the sub's: "sub" above "avg", no unit
                    cairo_text_extents(cr, "avg", &te);
                    cairo_move_to(cr, legendRight - te.x_advance, y2 - 4.0 * fs);
                    cairo_show_text(cr, "avg");
                    cairo_text_extents(cr, "sub", &te);
                    cairo_move_to(cr, legendRight - te.x_advance, y2 - 24.0 * fs);
                    cairo_show_text(cr, "sub");
                } else {
                    const char* left[2] = {"avg", unitLow};
                    for (int i = 0; i < 2; ++i) {
                        cairo_text_extents(cr, left[i], &te);
                        cairo_move_to(cr, legendRight - te.x_advance, y2 - (i == 0 ? 24.0 : 4.0) * fs);
                        cairo_show_text(cr, left[i]);
                    }
                }
                if (page == AppData::DP_GREEK_STAGE) {   // marks the cyan average as the substage's, not Trip's
                    cairo_set_source_rgb(cr, 0.0, 1.0, 1.0);
                    cairo_text_extents(cr, "sub", &te);
                    cairo_move_to(cr, legendRight - te.x_advance, y1 - 4.0 * fs);
                    cairo_show_text(cr, "sub");
                    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
                }
                const double rx = lcx + 272.0 * fs;
                cairo_move_to(cr, rx, y2 - 4.0 * fs);
                cairo_show_text(cr, "Total");
                if (page == AppData::DP_ITALIAN_TRIAL) {
                    cairo_move_to(cr, rx, y2 - 24.0 * fs);
                    cairo_show_text(cr, distUnit);
                } else {
                    cairo_set_source_rgb(cr, 0.0, 1.0, 1.0);
                    cairo_move_to(cr, rx, y1 - 4.0 * fs);
                    cairo_show_text(cr, data->state->rally_type == 2 ? "sub" : "Trip");
                    cairo_set_source_rgb(cr, 0.8, 0.8, 0.8);
                    cairo_move_to(cr, rx, (y1 + y2) / 2.0 + 2.0 * fs);
                    cairo_show_text(cr, distUnit);
                }
            }

            // ---- the strip: logo box | error box | right-hand box, one height, edges touching
            {
                std::string tag;
                double bg[3], fgc;
                int icon = 0;   // 0 none, 1 stopwatch, 2 speedo
                if (page == AppData::DP_ITALIAN_TRIAL) {
                    tag = v.tag;
                    if (v.isPc) { bg[0] = 0.867; bg[1] = 0.0; bg[2] = 0.0; fgc = 1.0; icon = 1; }
                    else        { bg[0] = 1.0; bg[1] = 0.867; bg[2] = 0.0; fgc = 0.0; icon = 2; }
                } else if (page == AppData::DP_GREEK_STAGE) {
                    tag = "S" + std::to_string(data->driverStageNo);
                    bg[0] = 0.867; bg[1] = 0.0; bg[2] = 0.0; fgc = 1.0; icon = 1;
                } else {
                    tag = "navi";
                    bg[0] = bg[1] = bg[2] = 0.4; fgc = 1.0;
                }
                const double w = 160.0 * fs, h = abBoxH;
                const double x0 = abBoxX - w, y0 = abBoxY;
                cairo_set_source_rgb(cr, bg[0], bg[1], bg[2]);
                cairo_rectangle(cr, x0, y0, w, h);
                cairo_fill(cr);
                cairo_set_source_rgb(cr, fgc, fgc, fgc);
                smallFace();
                cairo_set_font_size(cr, 46.0 * fs);
                cairo_text_extents(cr, tag.c_str(), &te);
                const double ic = icon ? 36.0 * fs : 0.0, gap = icon ? 8.0 * fs : 0.0;
                const double tx = x0 + (w - te.x_advance - gap - ic) / 2.0;
                cairo_move_to(cr, tx, y0 + h / 2.0 + 16.0 * fs);
                cairo_show_text(cr, tag.c_str());
                const double ox = tx + te.x_advance + gap + ic / 2.0;
                const double oy = y0 + (h - 40.0 * fs) / 2.0 + 24.0 * fs;
                cairo_set_line_width(cr, 4.0 * fs);
                if (icon == 1) {   // stopwatch: crown, round case, hand to 12
                    cairo_rectangle(cr, ox - 5.0 * fs, oy - 24.0 * fs, 10.0 * fs, 5.0 * fs);
                    cairo_fill(cr);
                    cairo_new_sub_path(cr);
                    cairo_arc(cr, ox, oy, 14.0 * fs, 0, 2 * M_PI);
                    cairo_stroke(cr);
                    cairo_move_to(cr, ox, oy);
                    cairo_line_to(cr, ox, oy - 10.0 * fs);
                    cairo_stroke(cr);
                } else if (icon == 2) {   // speedo: open dial, needle up-left
                    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
                    cairo_new_sub_path(cr);
                    cairo_arc(cr, ox, oy, 15.0 * fs, 0.75 * M_PI, 2.25 * M_PI);
                    cairo_stroke(cr);
                    cairo_move_to(cr, ox, oy);
                    cairo_line_to(cr, ox - 7.0 * fs, oy - 9.0 * fs);
                    cairo_stroke(cr);
                    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);
                }
            }

            // Right-hand box: the pc / pm countdown (green, -.- until 5 s, then 5.0 -> 0.0) or the navi minutes ahead /
            // behind (green ahead, red behind). None in the Greek pages.
            if (page == AppData::DP_ITALIAN_TRIAL
                || (page == AppData::DP_ITALIAN_NAVI && data->driverMinutesValid)) {
                const double w = 160.0 * fs, h = abBoxH;
                const double x0 = abBoxX + abBoxW, y0 = abBoxY;
                std::string txt;
                bool ahead = true;
                if (page == AppData::DP_ITALIAN_TRIAL) {
                    txt = countdownBoxText(v.toLineS);
                } else {
                    ahead = data->driverMinutes >= 0;
                    txt = (data->driverMinutes > 0 ? "+" : "") + std::to_string(data->driverMinutes);
                }
                if (ahead) cairo_set_source_rgb(cr, 0.0, 0.667, 0.0);   // green (#0a0)
                else       cairo_set_source_rgb(cr, 0.867, 0.067, 0.067);
                cairo_rectangle(cr, x0, y0, w, h);
                cairo_fill(cr);
                cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
                bigFace();
                cairo_set_font_size(cr, L.boxFontSize * 1.5);
                cairo_text_extents(cr, txt.c_str(), &te);
                const double ascent = -te.y_bearing, descent = te.height + te.y_bearing;
                cairo_move_to(cr, x0 + (w - te.x_advance) / 2.0, y0 + h / 2.0 + (ascent - descent) / 2.0);
                cairo_show_text(cr, txt.c_str());
                smallFace();
                // RB-DRV-20: legend right of the box, same font and x anchor as "km / Total"
                static_assert(DRIVER_PAGE_ITALIAN_TRIAL == AppData::DP_ITALIAN_TRIAL
                              && DRIVER_PAGE_ITALIAN_NAVI == AppData::DP_ITALIAN_NAVI,
                              "driverRightBoxLegend page numbers");
                cairo_set_font_size(cr, 18.0 * fs);
                cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
                const double textRight = x0 + (w + te.x_advance) / 2.0;
                const double legendX = captionLeftAfter(lcx + 272.0 * fs,
                                                        std::max(x0 + w, textRight), 6.0 * fs);
                cairo_move_to(cr, legendX, y0 + h / 2.0 + 6.0 * fs);
                cairo_show_text(cr, driverRightBoxLegend(page));
            }
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

        // (Single-display mode: the rally clock now sits above the gauge, on the page.)
    }

    // Pegged flat at the left or right end: the stage tag, the error box and
    // the Italian side box have now been painted over the bar. Draw the bar
    // and its chevron again so they sit on top of those boxes.
    if (!hideNeedle && max_val > 0.0 && std::abs(seconds) >= max_val) {
        drawNeedleBar();
        drawChevrons();
    }

    return FALSE;
}

void updateDriverDisplay(AppData* data) {
    auto current_poll = data->poller->getMostRecent();
    // Speed is measured on the raw count so it stays positive in reverse.
    auto current_raw_poll = data->poller->getMostRecentRaw();
    auto tenth_poll = data->poller->get10th();
    auto current_time_ms = getRallyTime_ms(*data->state);

    // How long an armed autostart still has to run. Computed once here
    // because two things downstream need it: the T- overlay near the end of
    // this function, and the time-error box, which holds at zero while an
    // "on the minute" autostart counts down (see autoStartHoldsTimeError).
    const int64_t autoStartDiff_ms = autoStartRemaining_ms(data);
    const bool holdTimeErrorAtZero = autoStartHoldActive(data);
    
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
    double current_speed = calculateCurrentSpeed(*data->state, current_raw_poll, tenth_poll);
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
    trip_speed = averageSpeedForDisplay(trip_speed, holdTimeErrorAtZero);
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
    total_speed = averageSpeedForDisplay(total_speed, holdTimeErrorAtZero);
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
    const bool dist_km = data->state->distance_km;
    const char* total_unit = dist_km ? "km" : "m";
    std::string total_dist_str = formatDistance(static_cast<double>(total_dist_m), dist_km);
    data->driverTotalDistText = total_dist_str;
    data->driverTotalUnitText = total_unit;

    long trip_dist_m = adjustedDistanceMeters(
        countsToCentimeters(trip_count_diff, data->state->calibration),
        data->state->trip_distance_adjust_cm);
    const char* trip_unit = dist_km ? "km" : "m";
    std::string trip_dist_str = formatDistance(static_cast<double>(trip_dist_m), dist_km);
    data->driverTripDistText = trip_dist_str;
    data->driverTripUnitText = trip_unit;
    data->driverTotalM = static_cast<double>(total_dist_m);
    data->driverTripM  = static_cast<double>(trip_dist_m);
    data->driverStageNo = data->state->last_memory_slot;
    // Stage arrival: the speed to reach the loaded stage's start with a minute spare (as Home).
    data->driverArrivalKph = -1;
    {
        const int slot = data->state->last_memory_slot;
        // Distance to the start belongs to greek and surrey only; an italian sector has none.
        if (data->state->rally_type != 2 && slot >= 1 && slot <= RallyState::MAX_MEMORY_SLOTS) {
            const long start_s = data->state->memory_slots[slot - 1].start_time_s;
            const long start_dist = data->state->memory_slots[slot - 1].start_distance_m;
            if (start_s >= 0) {
                time_t tt = current_time_ms / 1000;
                struct tm* lt = localtime(&tt);
                if (lt) {
                    const long tod = lt->tm_hour * 3600L + lt->tm_min * 60L + lt->tm_sec;
                    StageArrival arr = stageArrival(static_cast<double>(start_dist), (start_s - tod) / 60.0);
                    if (arr.valid) data->driverArrivalKph = arr.speed_kph;
                }
            }
        }
    }

    if (data->state->segment_current_number < 0) data->runSegIndex = -2;   // no stage: the next one starts a new run
    // Target speed and ahead/behind
    if (data->state->segment_current_number >= 0 && 
        data->state->segment_current_number < static_cast<long>(data->state->stage_segments.size())) {
        const Segment& seg = data->state->stage_segments[data->state->segment_current_number];
        double target_kph = countsPerHourToKPH(seg.target_speed_counts_per_hour, data->state->calibration);
        if (data->state->units) {
            target_kph = target_kph * 0.621371;  // Convert to MPH
        }
        ss.str("");
        ss << driverSpeedText(target_kph, data->state->rally_type);   // 2 dp in italian
        gtk_label_set_text(data->targetSpeedLabel, ss.str().c_str());
        gtk_label_set_text(data->gaugeTargetLabel, ss.str().c_str());
        // The coming change, in the same units as the value above it.
        double next_kph = 0.0;
        if (data->nextTargetSpeedLabel) {
            if (nextSegmentTargetKph(*data->state, &next_kph)) {
                // "> " marks it as the speed being moved TO, so it cannot be
                // misread as a second current value.
                std::stringstream ns;
                ns << "> " << driverSpeedText(data->state->units ? next_kph / 1.60934 : next_kph, data->state->rally_type);
                gtk_label_set_text(data->nextTargetSpeedLabel, ns.str().c_str());
            } else {
                gtk_label_set_text(data->nextTargetSpeedLabel, "");
            }
        }
        
        // Ahead/behind - calculated from stage start accounting for all segment speeds
        // Corrected, not raw: a wrong turn knocked off with -10 has to move
        // the time error too, or the gauge keeps crediting distance the crew
        // just told the box they had not driven.
        int64_t total_count_diff_ab = stageCountsCorrected(data, current_poll);
        // Italian: the pc / pm error inside one, at rest (tone silent) on navi / ct / co subs.
        double seconds = gaugeAheadSeconds(*data->state, current_poll.cntr1, current_poll.cntr2,
                                           current_time_ms, total_count_diff_ab);
        // Nothing to be ahead or behind of until the clock zeroes at the
        // minute. Held before the value is stored, so the gauge needle, the
        // chevrons and the tone all sit at rest too rather than tracking a
        // stage that is not running yet.
        if (holdTimeErrorAtZero) seconds = 0.0;
        
        // Store for gauge
        data->aheadBehindSeconds = seconds;

        // RB-DRV-18: inside an italian pc / pm the compact gauge shows its own layout
        // (sector total, pc / pm average, logo, 5 s countdown), every figure in km and km/h.
        if (holdTimeErrorAtZero) {
            data->driverTrial = DriverTrialView{};
        } else {
            data->driverTrial = driverTrialView(*data->state, current_poll.cntr1, current_poll.cntr2,
                                                current_time_ms);
        }
        {
            const RallyState& st = *data->state;
            const bool italian = st.rally_type == 2 && stageIsSector(st.stage_segments);
            data->driverPage = data->driverTrial.active ? AppData::DP_ITALIAN_TRIAL
                              : (italian ? AppData::DP_ITALIAN_NAVI : AppData::DP_GREEK_STAGE);
            if (data->driverPage == AppData::DP_ITALIAN_NAVI) {
                data->driverRowS = rowSeconds(seg);
                bool v_ok = false;
                const double ahead = sectorAheadSeconds(st, current_poll.cntr1, current_poll.cntr2,
                                                        current_time_ms, &v_ok);
                data->driverMinutesValid = v_ok;
                const long m = minutesInHand(ahead);
                data->driverMinutes = ahead >= 0.0 ? m : -m;
            }
        }
        if (data->driverTrial.active || data->driverPage == AppData::DP_ITALIAN_NAVI) {
            data->driverTrialTargetText = trialSpeedText(
                countsPerHourToKPH(seg.target_speed_counts_per_hour, data->state->calibration));
            double kph = data->smoothedSpeed;
            if (kph >= 0.0 && data->state->units) kph *= 1.609344;   // smoothedSpeed is mph then
            data->driverTrialSpeedText = kph < 0.0 ? "--.--" : trialSpeedText(kph);
        }

        // Fraction of the current segment driven, for the needle's scale chevrons
        // and segment-end tick. "In segment" while between the segment start and end.
        {
            int64_t seg_count_diff = segmentCountsCorrected(data, current_poll);
            {   // average speed over the run being driven: from the box where the speed last changed.
                // The run's start is taken when the segment changes: kept through a box that carries
                // the speed on, restarted at a speed change, the first box, or any step back.
                const long cur_seg = data->state->segment_current_number;
                if (cur_seg != data->runSegIndex) {
                    const bool carried = cur_seg == data->runSegIndex + 1 && cur_seg > 0 &&
                        !data->state->stage_segments[static_cast<size_t>(cur_seg)].changesSpeed;
                    if (!carried) {
                        data->runStartCntr1 = data->state->segment_start_cntr1;
                        data->runStartCntr2 = data->state->segment_start_cntr2;
                        data->runStartMs = data->state->segment_start_time_ms;
                        data->runStartAdjustCm = data->state->segment_start_adjust_cm;
                    }
                    data->runSegIndex = cur_seg;
                }
                // The original average function, fed the run's own start.
                const int64_t run_counts = correctedDistanceCounts(
                    calculateDistanceCounts(*data->state, current_poll.cntr1, current_poll.cntr2,
                                            data->runStartCntr1, data->runStartCntr2),
                    data->state->total_distance_adjust_cm - data->runStartAdjustCm, data->state->calibration);
                const double avg = calculateAverageSpeed(*data->state, data->runStartMs, current_time_ms,
                                                         run_counts, 0);
                data->driverSubAvgValid = (current_time_ms - data->runStartMs > 500 && avg > 0.0);
                data->driverSubAvgKph = data->driverSubAvgValid ? (data->state->units ? avg / 0.621371 : avg) : 0.0;
            }
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
            if (data->state->rally_type != 2) {
                // Greek and Surrey: the chevron works on TIME, the only thing known for sure. It walks the
                // scheduled row the stage clock is in, from its scheduled start to its scheduled end.
                bool in_row = false;
                const double stage_s = (current_time_ms - data->state->total_start_time_ms) / 1000.0;
                data->segmentProgress = scheduledRowProgress(data->state->stage_segments, stage_s, &in_row);
                data->inSegment = in_row;
            }
            if (data->driverPage == AppData::DP_ITALIAN_NAVI) {
                // Italian navi row: the chevron follows the countdown seconds, as in a pc / pm -- waits at the
                // start, reaches the end tick over the last 5 s, on the bar as the countdown reads 0.
                bool sec_ok = false;
                const double to_line = sectorSecondsToLine(*data->state, current_time_ms, &sec_ok);
                if (sec_ok) {
                    data->segmentProgress = finalWindowProgress(to_line);
                    data->inSegment = true;
                } else {
                    data->segmentProgress = 0.0;
                    data->inSegment = false;
                }
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
        // unaffected by the tone types) and the ahead/behind tone
        // (driven by whichever algorithm the type for the current side selects).
        double stage_dist_m = countsToMeters(total_count_diff_ab, data->state->calibration);
        // Italian: the tone runs from the moment a pc (its trial running) or a pm starts, not 250 m into the sector.
        if (data->state->rally_type == 2 && stage_dist_m < 250.0 &&
            (seg.type == SEG_PM || (seg.type == SEG_PC && data->state->trial_start_m >= 0.0)))
            stage_dist_m = 250.0;
        bool past_stage_end = stageDistanceComplete(data->state->stage_segments,
            stagePositionMm(*data->state, current_poll.cntr1, current_poll.cntr2));

        ArrowToneResult arrow = computeArrowBasedTone(
            data->arrowToneState,
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

        // One decision for the box's speaker and the phone (RB-WEB-02).
        data->currentTone = decideToneCadence(
            data->state->tone_enabled,
            simpleToneForError(seconds, data->state->tone_type_behind, data->state->tone_type_ahead),
            arrow,
            data->simpleToneState, seconds, stage_dist_m, past_stage_end);
        if (data->toneGen) {
            const ToneCadence& t = data->currentTone;
            data->toneGen->setCadence(t.tone_ms, t.silence_ms, t.freq_hz,
                t.square ? ToneWaveform::Square
                         : t.triangle ? ToneWaveform::Triangle : ToneWaveform::Sine);
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
        data->currentTone = ToneCadence{};
        if (data->toneGen) data->toneGen->setCadence(0, 0);
        data->aheadBehindSeconds = 0.0;
        data->segmentProgress = 0.0;
        data->inSegment = false;
        data->driverTrial = DriverTrialView{};
        data->driverPage = AppData::DP_GREEK_IDLE;
        if (data->rallyGaugeDrawingArea) {
            gtk_widget_queue_draw(data->rallyGaugeDrawingArea);
        }
        if (data->copilotGaugeArea) {
            gtk_widget_queue_draw(data->copilotGaugeArea);
        }
    }
    
    // Next segment info
    if (data->state->segment_current_number >= 0 && 
        data->state->segment_current_number < static_cast<long>(data->state->stage_segments.size()) - 1) {
        const Segment& current_seg = data->state->stage_segments[data->state->segment_current_number];
        int64_t seg_count_diff = segmentCountsCorrected(data, current_poll);
        
        double remaining_counts = current_seg.distance_counts - static_cast<double>(seg_count_diff);
        double remaining_m = countsToMeters(static_cast<int64_t>(remaining_counts), data->state->calibration);
        
        const Segment& next_seg = data->state->stage_segments[data->state->segment_current_number + 1];
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
        if (data->settingsFpsLabel) gtk_label_set_text(data->settingsFpsLabel, gtk_label_get_text(data->updatesPerSecLabel));
        if (data->settingsCpuLabel) gtk_label_set_text(data->settingsCpuLabel, gtk_label_get_text(data->cpuTempLabel));
        data->updateCount = 0;
        data->lastUpdateCountTime_ms = current_time_ms;
    }
    
    // Auto-start countdown overlay
    if (data->state->auto_start_rally_time_s > 0 && !data->autoStartTriggered) {
        const int64_t diff_ms = autoStartDiff_ms;

        if (autoStartCountdownVisible(data->state->auto_start_early_departure, diff_ms)) {
            int total_secs = static_cast<int>(diff_ms / 1000);
            int h = total_secs / 3600;
            int m = (total_secs % 3600) / 60;
            int s = total_secs % 60;
            char buf[32];
            snprintf(buf, sizeof(buf), "T- %02d:%02d:%02d", h, m, s);
            gtk_label_set_text(data->countdownLabel, buf);
            // Which kind of autostart is pending, so the crew can see at a
            // glance whether rolling before the minute is going to count.
            // One word to a line: on one line the caption is wider than the
            // countdown box above it and ran across the Distance and Total
            // figures either side of it.
            gtk_label_set_text(data->earlyDepartureLabel,
                data->state->auto_start_early_departure ? "Early\nDeparture\nENABLED"
                                                        : "Early\nDeparture\nDISABLED");
            if (data->countdownContainer) gtk_widget_show(data->countdownContainer);
        } else if (diff_ms > 0) {
            // Armed, but not yet inside the popup window: the clock runs, no popup.
            if (data->countdownContainer) gtk_widget_hide(data->countdownContainer);
        } else if (diff_ms <= 0 && diff_ms > -AUTO_START_TRIGGER_WINDOW_MS) {
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
        "label { color: #FFFFFF; font-weight: normal; }"
        "button { background-color: #333333; color: #FFFFFF; font-weight: bold; }"
        ".speed-header { font-size: 28px; }"
        ".speed-value-next { font-size: 51px; font-family: monospace; font-weight: bold; color: #FFDD00; }"
        ".speed-value { font-size: 64px; font-family: monospace; font-weight: bold; }"
        ".speed-value-xl { font-size: 80px; font-family: monospace; font-weight: bold; }"
        ".speed-value-target { font-size: 45px; font-family: monospace; font-weight: bold; }"
        ".target-info { font-size: 22px; }"
        ".ahead-behind { font-size: 28px; font-family: monospace; font-weight: bold; }"
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
    gtk_label_set_width_chars(data->nextTargetSpeedLabel, 8);
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
        ".countdown-label { font-size: 60px; font-family: monospace; color: white; }"
        ".early-departure-label { font-size: 22px; color: white; }",
        -1, nullptr);
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(countdownBox),
        GTK_STYLE_PROVIDER(cdProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 50);
    gtk_style_context_add_class(gtk_widget_get_style_context(countdownBox), "countdown-box");
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(GTK_WIDGET(data->countdownLabel)),
        GTK_STYLE_PROVIDER(cdProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 50);
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->countdownLabel)), "countdown-label");

    // "Early Departure" line, directly beneath the box.
    data->earlyDepartureLabel = GTK_LABEL(gtk_label_new(""));
    // Two lines, so they need centring on each other as well as on the box.
    gtk_label_set_justify(data->earlyDepartureLabel, GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(data->earlyDepartureLabel, 0.5);
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(GTK_WIDGET(data->earlyDepartureLabel)),
        GTK_STYLE_PROVIDER(cdProvider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 50);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(GTK_WIDGET(data->earlyDepartureLabel)), "early-departure-label");
    // Dropped only after the last add_provider above -- each of those takes
    // its own reference, so unreffing earlier leaves the remaining calls
    // working on a pointer kept alive purely by the contexts before them.
    g_object_unref(cdProvider);

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
