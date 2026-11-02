#include <memory>
#include "ui_home_italian.h"
#include "calculations.h"
#include "callbacks.h"
#include "counter_poller.h"
#include "rally_state.h"
#include "sector.h"
#include "stage_rows.h"
#include <cmath>
#include <cstdio>
#include <string>

namespace {

enum Align { LEFT, CENTRE, RIGHT };

void colour(cairo_t* cr, double r, double g, double b) { cairo_set_source_rgb(cr, r, g, b); }

void text(cairo_t* cr, const std::string& s, double x, double y, double px, Align a) {
    if (s.empty()) return;
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, px);
    cairo_text_extents_t e;
    cairo_text_extents(cr, s.c_str(), &e);
    cairo_move_to(cr, a == RIGHT ? x - e.x_advance : a == CENTRE ? x - e.x_advance / 2.0 : x, y);
    cairo_show_text(cr, s.c_str());
}

// The type tag as the time card colours it: a filled box with the tag text.
void tag(cairo_t* cr, const std::string& label, int type, double x, double y, double w, double h, bool faded) {
    double r = 0.4, g = 0.4, b = 0.4, tr = 1, tg = 1, tb = 1;
    switch (type) {
    case SEG_PC: r = 0.82; g = 0.06; b = 0.06; break;
    case SEG_CT: r = 0.12; g = 0.31; b = 0.78; break;
    case SEG_PM: r = 0.94; g = 0.82; b = 0.0; tr = tg = tb = 0; break;
    case SEG_CO: r = 0.06; g = 0.63; b = 0.19; break;
    default: break;
    }
    const double k = faded ? 0.45 : 1.0;
    colour(cr, r * k, g * k, b * k);
    cairo_rectangle(cr, x, y, w, h);
    cairo_fill(cr);
    colour(cr, tr * k, tg * k, tb * k);
    // Baseline that puts the ink of the label (descender of the p included) in the middle of the box.
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, h * 0.62);
    cairo_text_extents_t te;
    cairo_text_extents(cr, label.c_str(), &te);
    const double base = y + h / 2.0 - (te.y_bearing + te.height / 2.0);
    if (type == SEG_PC) {
        // a timed line carries the stopwatch after its name, as on the time card
        text(cr, label, x + w * 0.38, base, h * 0.62, CENTRE);
        const double d = h * 0.5, wx = x + w - d - 8, wy = y + (h - d) / 2.0 + 1;
        const double cx = wx + d / 2.0, cy = wy + d * 0.6, rr = d * 0.38;
        cairo_new_path(cr);
        cairo_set_line_width(cr, d / 8.0);
        cairo_arc(cr, cx, cy, rr, 0, 2 * M_PI);
        cairo_stroke(cr);
        cairo_move_to(cr, cx, cy);
        cairo_line_to(cr, cx, cy - rr * 0.7);
        cairo_stroke(cr);
        cairo_rectangle(cr, cx - d * 0.12, wy, d * 0.24, d * 0.12);
        cairo_fill(cr);
    } else {
        text(cr, label, x + w / 2.0, base, h * 0.62, CENTRE);
    }
}

std::string km2(double metres) {
    char b[24];
    snprintf(b, sizeof(b), "%.2f", metres / 1000.0);
    return b;
}

std::string kph2(double kph) {
    char b[24];
    snprintf(b, sizeof(b), "%.2f", kph);
    return b;
}

// m:ss.t, with a "+" and red when the line has gone by.
std::string tenths(double s) { return sectorSecondsText(s); }

// Local time of day, as the rally clock shows it (rally_ms is system time plus the rally offset).
std::string hhmm(int64_t rally_ms) {
    const time_t secs = static_cast<time_t>(rally_ms / 1000);
    struct tm tm_local;
    localtime_r(&secs, &tm_local);
    char b[16];
    snprintf(b, sizeof(b), "%02d:%02d", tm_local.tm_hour, tm_local.tm_min);
    return b;
}

double endOf(const std::vector<Segment>& segs, long i) {
    double m = 0.0;
    for (long k = 0; k <= i && k < static_cast<long>(segs.size()); k++) m += segs[k].distance_m;
    return m;
}

gboolean onDraw(GtkWidget* w, cairo_t* cr, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    const RallyState& live = *data->state;
    const int width = gtk_widget_get_allocated_width(w);
    const int height = gtk_widget_get_allocated_height(w);
    const int64_t now_ms = getRallyTime_ms(live);
    const CounterPoll poll = data->poller->getMostRecent();
    long stage_m = countsToCentimeters(stageCountsCorrected(data, poll), live.calibration) / 100;
    SectorPanel p = sectorPanel(live, now_ms, stage_m);
    // Before the sector starts, show the card as it will look at the first row (drawing only:
    // a copy of the state, the real one is never touched).
    std::unique_ptr<RallyState> preview;
    if (!p.running && sectorPreviewWanted(live)) {
        preview.reset(new RallyState(sectorPreviewState(live, now_ms)));
        const long first = 0;   // the sector's opening row
        stage_m = 0;            // standing at the line's start
        if (rowStartsTube(live.segments, first)) {
            preview->trial_start_ms = now_ms;
            preview->trial_start_m = stage_m;
        }
        p = sectorPanel(*preview, now_ms, stage_m);
        if (p.kind == SectorPanel::PC) { p.secondsToLine = live.segments[first].pc_duration_s; p.secondsValid = true; }
    }
    const RallyState& st = preview ? *preview : live;
    const std::vector<Segment>& segs = st.stage_segments;
    const long cur = st.segment_current_number;

    const int part = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "part"));
    // foot (its own area, left of the menu key): the pc just finished (error against the card) and the co's arrival
    if (part == 2) {
        if (p.running && st.last_pc_error.valid) {
            colour(cr, 0.0, 1.0, 1.0);
            text(cr, "error pc" + std::to_string(st.last_pc_error.ref), 0, 18, 13, LEFT);
            colour(cr, 1, 1, 1);
            char b[32];
            snprintf(b, sizeof(b), "%+.2f s", st.last_pc_error.seconds);
            text(cr, b, 0, 42, 19, LEFT);
            snprintf(b, sizeof(b), "%+ld m", st.last_pc_error.metres);
            text(cr, b, 0, 66, 19, LEFT);
        }
        if (p.running && p.coValid) {
            colour(cr, 0.0, 1.0, 1.0);
            text(cr, "co" + std::to_string(p.coRef) + " arrival", 174, 22, 16, CENTRE);
            colour(cr, 1, 1, 1);
            text(cr, hhmm(p.coInTimeMs), 174, 68, 40, CENTRE);
            colour(cr, 0.8, 0.8, 0.8);
            text(cr, "kph", 280, 22, 16, CENTRE);
            text(cr, "min", 352, 22, 16, CENTRE);
            colour(cr, 0.0, 1.0, 1.0);
            text(cr, std::to_string(p.coKph), 280, 68, 40, CENTRE);
            colour(cr, 1, 1, 1);
            text(cr, std::to_string(p.coMinutes), 352, 68, 40, CENTRE);
        }
        // Ahead / behind the roadbook schedule, to the nearest minute (0-9): green ahead or level, red behind.
        bool ahead_ok = false;
        const double ahead_s = sectorAheadSeconds(live, poll.cntr1, poll.cntr2, now_ms, &ahead_ok);
        if (p.running && ahead_ok) {
            colour(cr, 0.8, 0.8, 0.8);
            text(cr, "+/-", width - 39, 22, 16, CENTRE);
            const long mins = minutesInHand(ahead_s);
            if (ahead_s >= 0) colour(cr, 0.10, 0.60, 0.20); else colour(cr, 0.80, 0.10, 0.10);
            cairo_rectangle(cr, width - 74, 30, 70, 46);
            cairo_fill(cr);
            colour(cr, 1, 1, 1);
            text(cr, std::to_string(mins), width - 39, 68, 40, CENTRE);
        }
        return FALSE;
    }
    cairo_translate(cr, 0, -17);   // the table area starts 17 px lower, clear of the "now" key

    if (!p.running) {
        colour(cr, 0.6, 0.6, 0.6);
        text(cr, "no sector loaded", width / 2.0, height / 2.0, 26, CENTRE);
        return FALSE;
    }

    // The card runs from the stage columns across the blank right-hand key column (611 px).
    // Columns (right edges): km, progr, mm:ss, km/h.
    const double cKm = 214, cProgr = 350, cTime = 474, cKph = width - 2;
    {   // one layout for every row type: the pc table of the agreed mockup
        colour(cr, 1, 1, 1);
        text(cr, "km", cKm, 50, 18, RIGHT);
        text(cr, "progr", cProgr, 50, 18, RIGHT);
        text(cr, "mm:ss", cTime, 50, 18, RIGHT);
        text(cr, "km/h", cKph, 50, 18, RIGHT);
        colour(cr, 0.53, 0.53, 0.53);
        cairo_rectangle(cr, 0, 56, width, 3);
        cairo_fill(cr);
        // the pc being driven
        const Segment& row = segs[cur];
        tag(cr, p.inTag, row.type, 0, 82, 86, 40, false);
        colour(cr, 1, 1, 1);
        text(cr, km2(row.distance_m), cKm, 114, 36, RIGHT);
        colour(cr, 0.0, 1.0, 1.0);
        text(cr, km2(endOf(segs, cur)), cProgr, 114, 36, RIGHT);
        const double row_s = row.type == SEG_PC ? row.pc_duration_s
                           : (row.target_speed_kph > 0 ? row.distance_m / 1000.0 / row.target_speed_kph * 3600.0 : 0.0);
        if (row_s > 0) text(cr, formatMmSs(row_s), cTime, 114, 36, RIGHT);
        colour(cr, 1, 1, 1);
        text(cr, kph2(row.target_speed_kph), cKph, 114, 36, RIGHT);
        // count / down line, amber; red and counting up once the line has gone by
        colour(cr, 0.85, 0.65, 0.0);
        text(cr, "count", 0, 156, 15, LEFT);
        text(cr, "down", 0, 171, 15, LEFT);
        if (p.metresPast > 0) {   // RB-NAV-49: past the line, red, counting up
            colour(cr, 1.0, 0.15, 0.15);
            text(cr, "+" + km2(p.metresPast), cKm, 174, 32, RIGHT);
            colour(cr, 0.85, 0.65, 0.0);
        } else {
            text(cr, km2(p.metresToGo), cKm, 174, 32, RIGHT);
        }
        if (p.secondsValid) {
            if (p.secondsToLine < 0) colour(cr, 1.0, 0.15, 0.15);
            text(cr, tenths(p.secondsToLine), cTime, 174, 32, RIGHT);
            colour(cr, 0.85, 0.65, 0.0);
        }
        text(cr, kph2(p.avgKph), cKph, 174, 32, RIGHT);   // 0.00 until a speed has been worked out
        colour(cr, 0.53, 0.53, 0.53);
        cairo_rectangle(cr, 0, 186, width, 3);
        cairo_fill(cr);
        // the next line, faded
        if (cur + 1 < static_cast<long>(segs.size())) {
            const Segment& nx = segs[cur + 1];
            tag(cr, p.nextTag, nx.type, 0, 197, 86, 36, true);
            colour(cr, 0.45, 0.45, 0.45);
            text(cr, km2(nx.distance_m), cKm, 226, 32, RIGHT);
            colour(cr, 0.0, 0.5, 0.5);
            text(cr, km2(endOf(segs, cur + 1)), cProgr, 226, 32, RIGHT);
            const double nx_s = nx.type == SEG_PC ? nx.pc_duration_s
                              : (nx.target_speed_kph > 0 ? nx.distance_m / 1000.0 / nx.target_speed_kph * 3600.0 : 0.0);
            if (nx_s > 0) text(cr, formatMmSs(nx_s), cTime, 226, 32, RIGHT);
            colour(cr, 0.45, 0.45, 0.45);
            text(cr, kph2(nx.target_speed_kph), cKph, 226, 32, RIGHT);
        }
    }

    return FALSE;
}

}  // namespace

GtkWidget* createItalianCard(AppData* data, int w, int h, int part) {
    GtkWidget* a = gtk_drawing_area_new();
    g_object_set_data(G_OBJECT(a), "part", GINT_TO_POINTER(part));
    gtk_widget_set_size_request(a, w, h);
    g_signal_connect(a, "draw", G_CALLBACK(onDraw), data);
    return a;
}

void updateItalianCard(GtkWidget* card) { if (card) gtk_widget_queue_draw(card); }
