#include "ui_home.h"
#include "callbacks.h"
#include "calculations.h"
#include "counter_poller.h"
#include "config_file.h"
#include "nero_frame.h"
#include "rally_state.h"
#include "tone_generator.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>

namespace {

// Column geometry from home.html: 486 + 90 + 203 + 200 + 130 = 1109, four
// 39 px gaps = 156, total 1265 of the 1268 px panel.
constexpr int COL1_W = 486, COL2_W = 90, HIST_W = 203, STAGE_W = 200, RC_W = 130;
constexpr int COL_GAP = 39, KEY_H = 70, ROW_GAP = 36;
constexpr int KEY_W = 130;            // Total / Trip / next / prev and right-hand keys
constexpr int FIGURE_W = COL1_W - KEY_W;   // 356
constexpr int CELL_H = KEY_H + ROW_GAP;    // 106: a key and the strip under it
constexpr int CLOCK_H = 54, CLOCK_GAP = 26;
constexpr int HIST_H = 388 - CLOCK_H - CLOCK_GAP;   // 308
constexpr int STAGE_ROW_H = 54;
constexpr int STAGE_AREA_H = 388 - STAGE_ROW_H;     // 334

enum AreaKind { FIGURE, ELAPSED, HISTORY, STAGE, FLAG, FLAG_WATCH };

void setColour(cairo_t* cr, double r, double g, double b) { cairo_set_source_rgb(cr, r, g, b); }
void cyan(cairo_t* cr) { setColour(cr, 0.0, 1.0, 1.0); }
void white(cairo_t* cr) { setColour(cr, 1.0, 1.0, 1.0); }
void grey(cairo_t* cr) { setColour(cr, 0.8, 0.8, 0.8); }

enum Align { LEFT, CENTRE, RIGHT };

// Bold monospace text with its baseline at y, aligned about x.
void drawText(cairo_t* cr, const std::string& s, double x, double y, double px, Align a) {
    if (s.empty()) return;
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, px);
    cairo_text_extents_t e;
    cairo_text_extents(cr, s.c_str(), &e);
    double left = x;
    if (a == RIGHT) left = x - e.x_advance;
    else if (a == CENTRE) left = x - e.x_advance / 2.0;
    cairo_move_to(cr, left, y);
    cairo_show_text(cr, s.c_str());
}

// Small stopwatch, as Papworth draws it beside the elapsed times.
void drawWatch(cairo_t* cr, double x, double y, double size, bool filled_white) {
    if (filled_white) white(cr); else grey(cr);
    cairo_new_path(cr);   // drop the text's current point, or the arc is joined to it
    cairo_set_line_width(cr, size / 8.0);
    double cx = x + size / 2.0, cy = y + size * 0.6, r = size * 0.38;
    cairo_arc(cr, cx, cy, r, 0, 2 * M_PI);
    cairo_stroke(cr);
    cairo_move_to(cr, cx, cy);
    cairo_line_to(cr, cx, cy - r * 0.7);
    cairo_stroke(cr);
    cairo_rectangle(cr, cx - size * 0.12, y, size * 0.24, size * 0.12);
    cairo_fill(cr);
}

// Chequered flag on a pole, w wide.
void drawFlag(cairo_t* cr, double x, double y, double w, double h) {
    white(cr);
    cairo_rectangle(cr, x, y, 3, h);
    cairo_fill(cr);
    const int cols = 4, rows = 3;
    double cw = (w - 4) / cols, ch = (h * 0.6) / rows;
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) {
            if ((r + c) % 2 == 0) white(cr); else setColour(cr, 0.25, 0.25, 0.25);
            cairo_rectangle(cr, x + 4 + c * cw, y + 1 + r * ch, cw, ch);
            cairo_fill(cr);
        }
    white(cr);
    cairo_set_line_width(cr, 1.0);
    cairo_rectangle(cr, x + 4, y + 1, cw * cols, ch * rows);
    cairo_stroke(cr);
}

gboolean onDraw(GtkWidget* w, cairo_t* cr, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    HomeUi* h = data->home;
    if (!h) return FALSE;
    const int kind = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "kind"));
    const int idx = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "idx"));
    const int width = gtk_widget_get_allocated_width(w);

    switch (kind) {
    case FIGURE:
        // 88 px digits, right-aligned; their foot sits a quarter of the way
        // into the gap below the key (key bottom + 9).
        white(cr);
        drawText(cr, h->figure[idx], width, KEY_H + 9, 88, RIGHT);
        break;
    case ELAPSED:
        if (idx == 2) {
            grey(cr);
            drawText(cr, h->speedLine, width / 2.0, 22, 22, CENTRE);
        } else {
            // "54:21" right-aligned to the key edge with a stopwatch before it.
            grey(cr);
            drawText(cr, h->elapsed[idx], width, 22, 26, RIGHT);
            cairo_text_extents_t e;
            cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 26);
            cairo_text_extents(cr, h->elapsed[idx].c_str(), &e);
            drawWatch(cr, width - e.x_advance - 22, 4, 18, false);
        }
        break;
    case HISTORY: {
        cyan(cr);
        drawText(cr, "Trip history", width / 2.0, 20, 20, CENTRE);
        white(cr);
        // Figures right-aligned inside a block sized for "99,999" (6 x 24 px),
        // and that block centred in the column.
        const double right = (width + 6 * 24) / 2.0;
        for (int i = 0; i < 6; i++)
            drawText(cr, h->history[i], right, 24 + (i + 1) * 46 - 10, 40, RIGHT);
        break;
    }
    case STAGE: {
        // auto / manual line
        const double ay = 12 + 28;
        // Always the word "auto"; when no autostart is armed (and none has started
        // the running stage) it is crossed out in red.
        cyan(cr);
        drawText(cr, "auto", 2, ay, 18, LEFT);
        if (!h->autoArmed) {
            cairo_text_extents_t e;
            cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 18);
            cairo_text_extents(cr, "auto", &e);
            setColour(cr, 1.0, 0.1, 0.1);
            cairo_set_line_width(cr, 3.0);
            cairo_move_to(cr, 0, ay - 14);
            cairo_line_to(cr, 4 + e.x_advance, ay + 2);
            cairo_move_to(cr, 0, ay + 2);
            cairo_line_to(cr, 4 + e.x_advance, ay - 14);
            cairo_stroke(cr);
        }
        white(cr);
        drawText(cr, h->autoTime, width - 4, ay, 30, RIGHT);

        // segment table: KPH / at m
        const double top = 12 + 36 + 6;
        cyan(cr);
        drawText(cr, "KPH", 79, top + 18, 20, RIGHT);
        drawText(cr, "at m", 193, top + 18, 20, RIGHT);
        for (int i = 0; i < 3; i++) {
            if (h->rows[i].kph.empty()) continue;
            if (h->rows[i].now) cyan(cr); else white(cr);
            drawText(cr, h->rows[i].kph, 79, top + 22 + (i + 1) * 36 - 8, 30, RIGHT);
            drawText(cr, h->rows[i].at, 193, top + 22 + (i + 1) * 36 - 8, 30, RIGHT);
        }

        // Stage arrival: speed to arrive a minute early, and minutes to go.
        const double base = STAGE_AREA_H - 130;
        cyan(cr);
        drawText(cr, "Stage arrival", width / 2.0, base + 26, 22, CENTRE);
        grey(cr);
        drawText(cr, "kph", width * 0.25, base + 62, 18, CENTRE);
        drawText(cr, "min", width * 0.75, base + 62, 18, CENTRE);
        if (h->arrivalValid) {
            cyan(cr);
            drawText(cr, std::to_string(h->arrivalKph), width * 0.25, base + 112, 40, CENTRE);
            white(cr);
            drawText(cr, std::to_string(h->arrivalMin), width * 0.75, base + 112, 40, CENTRE);
        }
        break;
    }
    case FLAG:
    case FLAG_WATCH: {
        const int ww = width;
        if (kind == FLAG) {
            drawFlag(cr, ww / 2.0 - 20, 1, 40, 30);
            white(cr);
            // Read from the clock every time the key is drawn, never from a
            // stored copy, so it can not be stale or blank: it is always the
            // coming minute.
            drawText(cr, nextMinuteCaption(data), ww / 2.0, 50, 24, CENTRE);
        } else {
            if (stageAbortable(*data->state)) {
                // A stage is armed or running: this key ends it.
                white(cr);
                drawText(cr, "stage", ww / 2.0, 22, 22, CENTRE);
                drawText(cr, "abort", ww / 2.0, 46, 22, CENTRE);
            } else {
                drawFlag(cr, ww / 2.0 - 50, 12, 40, 30);
                drawWatch(cr, ww / 2.0 + 10, 9, 34, true);
            }
        }
        break;
    }
    }
    return FALSE;
}

GtkWidget* makeArea(AppData* data, AreaKind kind, int idx, int w, int h) {
    GtkWidget* a = gtk_drawing_area_new();
    gtk_widget_set_size_request(a, w, h);
    g_object_set_data(G_OBJECT(a), "kind", GINT_TO_POINTER(kind));
    g_object_set_data(G_OBJECT(a), "idx", GINT_TO_POINTER(idx));
    g_signal_connect(a, "draw", G_CALLBACK(onDraw), data);
    return a;
}

void addClass(GtkWidget* w, const char* cls) {
    gtk_style_context_add_class(gtk_widget_get_style_context(w), cls);
}

GtkWidget* makeKey(const char* label, int w, int h, GCallback cb, AppData* data) {
    GtkWidget* b = gtk_button_new_with_label(label);
    gtk_widget_set_size_request(b, w, h);
    addClass(b, "dist-heading");
    if (cb) g_signal_connect(b, "clicked", cb, data);
    return b;
}

// A key whose face is a drawing (a flag), so it takes the area as its child.
GtkWidget* makeIconKey(AppData* data, AreaKind kind, GCallback cb, HomeUi* h) {
    GtkWidget* b = gtk_button_new();
    gtk_widget_set_size_request(b, KEY_W, KEY_H);
    addClass(b, "dist-heading");
    // 16 px smaller than the key each way: the button adds about 12 px of
    // padding and border round its child, and a child as big as the key made
    // these two keys 74 px tall, throwing the column's pitch out against the
    // 70 px keys on the left.
    GtkWidget* area = makeArea(data, kind, 0, KEY_W - 16, KEY_H - 16);
    gtk_container_add(GTK_CONTAINER(b), area);
    g_signal_connect(b, "clicked", cb, data);
    (void)h;
    return b;
}

// Make the key look "engaged": white with black text.
void setLit(GtkWidget* key, bool lit) {
    GtkStyleContext* c = gtk_widget_get_style_context(key);
    if (lit) gtk_style_context_add_class(c, "key-lit");
    else gtk_style_context_remove_class(c, "key-lit");
}

void markDirty(GtkWidget* w) { if (w) gtk_widget_queue_draw(w); }

// ── actions ──────────────────────────────────────────────────────────────

void onFreeze(GtkWidget*, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    data->homeFrozen = !data->homeFrozen;
    setLit(data->home->freezeBtn, data->homeFrozen);
}

// The repeating "whoop" while reversed.
gboolean whoopTick(gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    if (!data->poller || !data->poller->reverse()) {
        if (data->home) data->home->whoopTimer = 0;
        return G_SOURCE_REMOVE;
    }
    if (data->toneGen) data->toneGen->playBeep(392.0, ToneWaveform::Triangle, 250, 0.40);
    return G_SOURCE_CONTINUE;
}

void onReverse(GtkWidget*, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    setReverse(data, !data->poller->reverse());
}

void onNextMinute(GtkWidget*, gpointer user_data) {
    startStageAtNextMinute(static_cast<AppData*>(user_data));
}

// Stage go now -- or, once a stage is armed or running, stage abort. Ending a
// stage takes a confirm: one stray tap must not do it.
void onLateArrival(GtkWidget* w, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    if (stageAbortable(*data->state)) {
        if (confirmYesNo(w, "Abort stage", "ABORT the stage?")) performAbortStage(data);
        return;
    }
    performStageGo(data);
}

// Step to the next populated memory in a direction; no wrap, empties skipped.
void stepStage(AppData* data, int dir) {
    int cur = data->state->last_memory_slot;   // 0 = none loaded
    for (int s = (cur == 0 ? (dir > 0 ? 0 : RallyState::MAX_MEMORY_SLOTS + 1) : cur) + dir;
         s >= 1 && s <= RallyState::MAX_MEMORY_SLOTS; s += dir) {
        if (!data->state->memory_slots[s - 1].empty()) {
            recallMemorySlot(data, s);
            return;
        }
    }
}
void onStageMinus(GtkWidget*, gpointer d) { stepStage(static_cast<AppData*>(d), -1); }
void onStagePlus(GtkWidget*, gpointer d) { stepStage(static_cast<AppData*>(d), +1); }

void onStageNumber(GtkWidget*, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    HomeUi* h = data->home;
    for (int i = 0; i < 5; i++) {
        const bool empty = data->state->memory_slots[i].empty();
        gtk_widget_set_sensitive(h->pickerBtns[i], !empty);
        setLit(h->pickerBtns[i], data->state->last_memory_slot == i + 1);
    }
    gtk_stack_set_visible_child_name(GTK_STACK(h->stack), "picker");
}

void onPick(GtkWidget* w, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    recallMemorySlot(data, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "slot")));
    gtk_stack_set_visible_child_name(GTK_STACK(data->home->stack), "main");
}

void onPickerCancel(GtkWidget*, gpointer user_data) {
    gtk_stack_set_visible_child_name(GTK_STACK(static_cast<AppData*>(user_data)->home->stack), "main");
}

// The picker hides itself whenever Home is left, so it never greets the crew
// the next time they arrive.
void onHomeUnmap(GtkWidget*, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    if (data->home && data->home->stack)
        gtk_stack_set_visible_child_name(GTK_STACK(data->home->stack), "main");
}

} // namespace

void setReverse(AppData* data, bool on) {
    if (!data->poller || data->poller->reverse() == on) return;
    data->poller->setReverse(on);
    HomeUi* h = data->home;
    if (h) setLit(h->reverseBtn, on);
    // Keep the raw-minus-distance shift with the baselines in the config, so
    // a restart after reversing still lines up.
    data->state->reverse_shift1 = data->poller->virtualShift1();
    data->state->reverse_shift2 = data->poller->virtualShift2();
    ConfigFile::save(*data->state);
    if (h) {
        if (on && h->whoopTimer == 0) {
            whoopTick(data);
            h->whoopTimer = g_timeout_add(700, whoopTick, data);
        } else if (!on && h->whoopTimer != 0) {
            g_source_remove(h->whoopTimer);
            h->whoopTimer = 0;
        }
    }
}

GtkWidget* createHomeScreen(AppData* data) {
    HomeUi* h = new HomeUi();
    data->home = h;

    // The existing update code writes these; they are never shown on the new
    // page (the figures are drawn instead), but must exist.
    data->totalDistLabel = GTK_LABEL(gtk_label_new("0"));
    data->totalUnitLabel = GTK_LABEL(gtk_label_new("m"));
    data->totalTimeLabel = GTK_LABEL(gtk_label_new("0:00"));
    data->tripDistLabel = GTK_LABEL(gtk_label_new("0"));
    data->tripUnitLabel = GTK_LABEL(gtk_label_new("m"));
    data->tripTimeLabel = GTK_LABEL(gtk_label_new("0:00"));
    data->nextDistLabel = GTK_LABEL(gtk_label_new("---.---"));
    data->nextUnitLabel = GTK_LABEL(gtk_label_new("m"));
    data->nextSpeedLabel = GTK_LABEL(gtk_label_new("---"));
    data->stageStatusLabel = nullptr;
    data->alarmCountdownLabel = nullptr;   // the Alarm KM grid is gone
    data->alarmClearBtn = nullptr;

    GtkWidget* screen = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, COL_GAP);
    gtk_container_set_border_width(GTK_CONTAINER(screen), 5);
    g_signal_connect(screen, "unmap", G_CALLBACK(onHomeUnmap), data);

    // ── column 1: Total / Trip / next / prev with their figures ──
    // Columns 1 and 2 sit in an overlay so freeze and reverse can be placed by
    // position along the bottom row: reverse ends level with the right edge of
    // the figures above it.
    GtkWidget* leftOverlay = gtk_overlay_new();
    GtkWidget* leftCols = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, COL_GAP);
    gtk_container_add(GTK_CONTAINER(leftOverlay), leftCols);
    gtk_box_pack_start(GTK_BOX(screen), leftOverlay, FALSE, FALSE, 0);

    GtkWidget* c1 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(c1, COL1_W, -1);
    gtk_box_pack_start(GTK_BOX(leftCols), c1, FALSE, FALSE, 0);

    struct { const char* label; GCallback cb; } rows[3] = {
        { "total", G_CALLBACK(on_total_reset) },
        { "trip", G_CALLBACK(on_trip_reset) },
        { "next", G_CALLBACK(on_next_press) },
    };
    for (int i = 0; i < 3; i++) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
        GtkWidget* left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        GtkWidget* key = makeKey(rows[i].label, KEY_W, KEY_H, rows[i].cb, data);
        gtk_box_pack_start(GTK_BOX(left), key, FALSE, FALSE, 0);
        h->elapsedArea[i] = makeArea(data, ELAPSED, i, KEY_W, ROW_GAP);
        gtk_box_pack_start(GTK_BOX(left), h->elapsedArea[i], FALSE, FALSE, 0);
        h->figureArea[i] = makeArea(data, FIGURE, i, FIGURE_W, CELL_H);
        gtk_box_pack_start(GTK_BOX(row), left, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), h->figureArea[i], FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(c1), row, FALSE, FALSE, 0);
        if (i == 2) data->nextPrevBtn = key;
        gtk_widget_set_sensitive(key, i == 2 ? FALSE : TRUE);
    }
    // prev, then freeze and reverse further right (placed over the columns below)
    {
        data->prevBtn = makeKey("prev", KEY_W, KEY_H, G_CALLBACK(on_prev_press), data);
        gtk_widget_set_sensitive(data->prevBtn, FALSE);
        gtk_box_pack_start(GTK_BOX(c1), data->prevBtn, FALSE, FALSE, 0);
        gtk_widget_set_halign(data->prevBtn, GTK_ALIGN_START);   // 130 px, not the column's width
        h->freezeBtn = makeKey("freeze", KEY_W, KEY_H, G_CALLBACK(onFreeze), data);
        h->reverseBtn = makeKey("reverse", KEY_W, KEY_H, G_CALLBACK(onReverse), data);
        // 24 px: "reverse" at the keys' usual 28 px is wider than 130 px once the
        // button's padding is added, and was clipped at the overlay's edge.
        addClass(h->freezeBtn, "key-24");
        addClass(h->reverseBtn, "key-24");
    }

    // ── column 2: +10 / set / −10, level with Total / Trip / next ──
    GtkWidget* c2 = gtk_box_new(GTK_ORIENTATION_VERTICAL, ROW_GAP);
    gtk_widget_set_size_request(c2, COL2_W, -1);
    gtk_box_pack_start(GTK_BOX(leftCols), c2, FALSE, FALSE, 0);
    struct { const char* label; int delta_m; } adj[3] = { {"+10", 10}, {"set", 0}, {"-10", -10} };
    for (int i = 0; i < 3; i++) {
        GtkWidget* b = makeKey(adj[i].label, COL2_W, KEY_H, nullptr, data);
        addClass(b, "key-small");
        if (adj[i].delta_m == 0) {
            g_signal_connect(b, "clicked", G_CALLBACK(on_distance_set), data);
        } else {
            g_object_set_data(G_OBJECT(b), "delta_m", GINT_TO_POINTER(adj[i].delta_m));
            g_signal_connect(b, "clicked", G_CALLBACK(on_distance_adjust), data);
        }
        gtk_box_pack_start(GTK_BOX(c2), b, FALSE, FALSE, 0);
    }

    {
        // Reverse is right-aligned with the Total / Trip / next figures (owner's
        // ruling; it first lined up with -10, which was too far right).
        // The drawn digits stop a few pixels short of the figure area's edge
        // (the glyphs' own side bearing), so the key's right edge is set 10 px in
        // to sit under the last digit, measured on the sandbox screenshot.
        const int right = COL1_W - 10;                        // 476
        // The keys' real widths (a button is a little wider than its 130 px
        // request when the label needs it), so reverse ends exactly on -10's edge.
        gint rev_w = KEY_W, frz_w = KEY_W, nat = 0;
        gtk_widget_get_preferred_width(h->reverseBtn, nullptr, &nat); rev_w = std::max<gint>(KEY_W, nat);
        gtk_widget_get_preferred_width(h->freezeBtn, nullptr, &nat);  frz_w = std::max<gint>(KEY_W, nat);
        const int rev_x = right - rev_w;
        const int frz_x = rev_x - 24 - frz_w;
        const int row_y = 3 * (KEY_H + ROW_GAP);              // 318: the prev row
        GtkWidget* fixed = gtk_fixed_new();
        gtk_fixed_put(GTK_FIXED(fixed), h->freezeBtn, frz_x, row_y);
        gtk_fixed_put(GTK_FIXED(fixed), h->reverseBtn, rev_x, row_y);
        gtk_widget_set_halign(fixed, GTK_ALIGN_START);
        gtk_widget_set_valign(fixed, GTK_ALIGN_START);
        gtk_overlay_add_overlay(GTK_OVERLAY(leftOverlay), fixed);
        gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(leftOverlay), fixed, TRUE);
    }

    // ── columns 3 and 4 live in a stack so the picker can cover both ──
    h->stack = gtk_stack_new();
    gtk_box_pack_start(GTK_BOX(screen), h->stack, FALSE, FALSE, 0);

    GtkWidget* mainPage = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, COL_GAP);
    gtk_stack_add_named(GTK_STACK(h->stack), mainPage, "main");

    // column 3: clock over Trip history
    GtkWidget* c3 = gtk_box_new(GTK_ORIENTATION_VERTICAL, CLOCK_GAP);
    gtk_widget_set_size_request(c3, HIST_W, -1);
    gtk_box_pack_start(GTK_BOX(mainPage), c3, FALSE, FALSE, 0);
    gtk_label_set_xalign(data->copilotRallyClockLabel, 0.5);
    gtk_label_set_width_chars(data->copilotRallyClockLabel, 8);
    gtk_widget_set_size_request(GTK_WIDGET(data->copilotRallyClockLabel), HIST_W, CLOCK_H);
    addClass(GTK_WIDGET(data->copilotRallyClockLabel), "home-clock");
    gtk_box_pack_start(GTK_BOX(c3), GTK_WIDGET(data->copilotRallyClockLabel), FALSE, FALSE, 0);
    h->historyArea = makeArea(data, HISTORY, 0, HIST_W, HIST_H);
    gtk_box_pack_start(GTK_BOX(c3), h->historyArea, FALSE, FALSE, 0);

    // column 4: stage - n +, then auto / segment table / Stage arrival
    GtkWidget* c4 = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(c4, STAGE_W, -1);
    gtk_box_pack_start(GTK_BOX(mainPage), c4, FALSE, FALSE, 0);
    GtkWidget* stageRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_size_request(stageRow, STAGE_W, STAGE_ROW_H);
    gtk_widget_set_halign(stageRow, GTK_ALIGN_CENTER);
    GtkWidget* minus = makeKey("-", 52, 52, G_CALLBACK(onStageMinus), data);
    h->stageNoBtn = makeKey("-", 50, 50, G_CALLBACK(onStageNumber), data);
    GtkWidget* plus = makeKey("+", 52, 52, G_CALLBACK(onStagePlus), data);
    gtk_box_pack_start(GTK_BOX(stageRow), minus, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(stageRow), h->stageNoBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(stageRow), plus, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(c4), stageRow, FALSE, FALSE, 0);
    h->stageArea = makeArea(data, STAGE, 0, STAGE_W, STAGE_AREA_H);
    gtk_box_pack_start(GTK_BOX(c4), h->stageArea, FALSE, FALSE, 0);

    // picker page: five keys, covering columns 3 and 4 (203 + 39 + 200 = 442)
    GtkWidget* picker = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_size_request(picker, HIST_W + COL_GAP + STAGE_W, -1);
    gtk_widget_set_valign(picker, GTK_ALIGN_CENTER);
    GtkWidget* title = gtk_label_new("Load stage memory");
    addClass(title, "home-picker-title");
    gtk_box_pack_start(GTK_BOX(picker), title, FALSE, FALSE, 0);
    GtkWidget* keys = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(keys, GTK_ALIGN_CENTER);
    for (int i = 0; i < 5; i++) {
        GtkWidget* b = makeKey(std::to_string(i + 1).c_str(), 70, 70, G_CALLBACK(onPick), data);
        g_object_set_data(G_OBJECT(b), "slot", GINT_TO_POINTER(i + 1));
        h->pickerBtns[i] = b;
        gtk_box_pack_start(GTK_BOX(keys), b, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(picker), keys, FALSE, FALSE, 0);
    GtkWidget* cancel = makeKey("cancel", 130, 56, G_CALLBACK(onPickerCancel), data);
    gtk_widget_set_halign(cancel, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(picker), cancel, FALSE, FALSE, 0);
    gtk_stack_add_named(GTK_STACK(h->stack), picker, "picker");
    gtk_stack_set_visible_child_name(GTK_STACK(h->stack), "main");

    // ── column 5: flags, zero, menu ──
    GtkWidget* c5 = gtk_box_new(GTK_ORIENTATION_VERTICAL, ROW_GAP);
    gtk_widget_set_size_request(c5, RC_W, -1);
    gtk_box_pack_start(GTK_BOX(screen), c5, FALSE, FALSE, 0);
    h->minuteCaption = gtk_label_new("");   // text carrier for the flag key's caption
    g_object_ref_sink(h->minuteCaption);
    GtkWidget* flagKey = makeIconKey(data, FLAG, G_CALLBACK(onNextMinute), h);
    h->flagArea = gtk_bin_get_child(GTK_BIN(flagKey));
    gtk_box_pack_start(GTK_BOX(c5), flagKey, FALSE, FALSE, 0);
    GtkWidget* goKey = makeIconKey(data, FLAG_WATCH, G_CALLBACK(onLateArrival), h);
    h->goArea = gtk_bin_get_child(GTK_BIN(goKey));
    gtk_box_pack_start(GTK_BOX(c5), goKey, FALSE, FALSE, 0);
    data->adjZeroBtn = makeKey("A-B, B-C\nzero", RC_W, KEY_H, G_CALLBACK(on_adj_driver_zero), data);
    addClass(data->adjZeroBtn, "key-small");
    gtk_label_set_justify(GTK_LABEL(gtk_bin_get_child(GTK_BIN(data->adjZeroBtn))), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(c5), data->adjZeroBtn, FALSE, FALSE, 0);
    GtkWidget* menu = createMenuKey(data);
    gtk_widget_set_size_request(menu, RC_W, KEY_H);
    gtk_box_pack_start(GTK_BOX(c5), menu, FALSE, FALSE, 0);

    return screen;
}

void updateHomeDisplay(AppData* data) {
    HomeUi* h = data->home;
    if (!h) return;

    // Freeze holds the Total / Trip / next readouts and the Trip history
    // (below). The clock, the flag caption and the stage panel keep running.
    const int64_t now_ms = getRallyTime_ms(*data->state);

    // hh:mm under the next-minute flag
    std::string cap = nextMinuteCaption(data);
    if (cap != gtk_label_get_text(GTK_LABEL(h->minuteCaption))) {
        gtk_label_set_text(GTK_LABEL(h->minuteCaption), cap.c_str());
        markDirty(h->flagArea);
    }

    // The stage-go key turns into stage abort while a stage is armed or running.
    {
        const bool abortable = stageAbortable(*data->state);
        if (abortable != h->goWasAbort) {
            h->goWasAbort = abortable;
            markDirty(h->goArea);
        }
    }

    // Stage panel: loaded memory, auto / manual, table, arrival. Not part of
    // the frozen readouts.
    {
        const int slot = data->state->last_memory_slot;
        std::string no = (slot >= 1 && slot <= RallyState::MAX_MEMORY_SLOTS) ? std::to_string(slot) : "-";
        if (no != h->stageNo) {
            h->stageNo = no;
            gtk_button_set_label(GTK_BUTTON(h->stageNoBtn), no.c_str());
        }

        long start_s = -1, start_dist = 0;
        if (slot >= 1 && slot <= RallyState::MAX_MEMORY_SLOTS) {
            start_s = data->state->memory_slots[slot - 1].start_time_s;
            start_dist = data->state->memory_slots[slot - 1].start_distance_m;
        }
        std::string armed = formatAutoStartStatus(data->state->auto_start_rally_time_s,
                                                  data->state->auto_start_early_departure,
                                                  getAutoStartEpochMs());
        // Armed and pending, or the autostart that started the stage now running:
        // both read "auto hh:mm:ss". Only with neither does it read "manual".
        if (armed == "none" && data->autoFiredTime_s != 0 && !data->state->stage_complete)
            armed = formatAutoStartStatus(data->autoFiredTime_s, false, getAutoStartEpochMs());
        const bool is_armed = (armed != "none");
        std::string auto_time = armed;
        if (!is_armed) {
            auto_time.clear();
            if (start_s >= 0) {
                char b[16];
                snprintf(b, sizeof(b), "%02ld:%02ld:%02ld", start_s / 3600, (start_s / 60) % 60, start_s % 60);
                auto_time = b;
            }
        }

        // First rows of the loaded roadbook (the running one while a stage is
        // under way), the current segment lit.
        const std::vector<Segment>& segs =
            (data->state->segment_current_number >= 0 && !data->state->stage_segments.empty())
                ? data->state->stage_segments : data->state->segments;
        long cur = data->state->segment_current_number;
        long first = (cur > 0) ? std::min<long>(cur - 1, std::max<long>(0, static_cast<long>(segs.size()) - 3)) : 0;
        HomeUi::Row rows[3];
        double cum_m = 0.0;
        for (long i = 0; i < first && i < static_cast<long>(segs.size()); i++) cum_m += segs[i].distance_m;
        for (int r = 0; r < 3; r++) {
            long i = first + r;
            if (i >= static_cast<long>(segs.size())) break;
            double kph = segs[i].target_speed_kph * (data->state->units ? 0.621371 : 1.0);
            char b[16];
            snprintf(b, sizeof(b), "%.1f", kph);
            rows[r].kph = b;
            rows[r].at = formatDistanceGrouped(static_cast<long>(std::lround(cum_m)));
            // grouped figures keep their commas, as in the mockup ("1,500")
            rows[r].now = (i == cur);
            cum_m += segs[i].distance_m;
        }

        // Stage arrival: minutes until the loaded memory's start time.
        StageArrival arr;
        if (start_s >= 0) {
            time_t t = now_ms / 1000;
            struct tm* lt = localtime(&t);
            if (lt) {
                long tod = lt->tm_hour * 3600L + lt->tm_min * 60L + lt->tm_sec;
                arr = stageArrival(static_cast<double>(start_dist), (start_s - tod) / 60.0);
            }
        }

        bool changed = (is_armed != h->autoArmed) || (auto_time != h->autoTime)
                    || (arr.valid != h->arrivalValid) || (arr.speed_kph != h->arrivalKph)
                    || (arr.minutes != h->arrivalMin);
        for (int r = 0; r < 3; r++) {
            if (rows[r].kph != h->rows[r].kph || rows[r].at != h->rows[r].at || rows[r].now != h->rows[r].now)
                changed = true;
            h->rows[r] = rows[r];
        }
        h->autoArmed = is_armed;
        h->autoTime = auto_time;
        h->arrivalValid = arr.valid;
        h->arrivalKph = arr.speed_kph;
        h->arrivalMin = arr.minutes;
        if (changed) markDirty(h->stageArea);
    }

    if (data->homeFrozen) return;

    // Drawn mirrors of the TwinMaster readouts that updateCopilotDisplay just wrote.
    auto mirror = [&](std::string& dst, const char* src, GtkWidget* area) {
        if (dst != src) { dst = src; markDirty(area); }
    };
    mirror(h->figure[0], gtk_label_get_text(data->totalDistLabel), h->figureArea[0]);
    mirror(h->figure[1], gtk_label_get_text(data->tripDistLabel), h->figureArea[1]);
    // "---.---" (no segment) is wider than the 88 px figure column.
    std::string next_fig = gtk_label_get_text(data->nextDistLabel);
    if (!next_fig.empty() && next_fig[0] == '-' && next_fig.find('.') != std::string::npos) next_fig = "---";
    mirror(h->figure[2], next_fig.c_str(), h->figureArea[2]);
    mirror(h->elapsed[0], gtk_label_get_text(data->totalTimeLabel), h->elapsedArea[0]);
    mirror(h->elapsed[1], gtk_label_get_text(data->tripTimeLabel), h->elapsedArea[1]);

    // "50<>60 kph": this segment's speed and the next one's.
    std::string line;
    const RallyState& st = *data->state;
    if (st.segment_current_number >= 0 &&
        st.segment_current_number + 1 < static_cast<long>(st.stage_segments.size())) {
        line = speedChangeLine(st.stage_segments[st.segment_current_number].target_speed_kph,
                               st.stage_segments[st.segment_current_number + 1].target_speed_kph,
                               true, st.units);
    }
    mirror(h->speedLine, line.c_str(), h->elapsedArea[2]);

    std::vector<long> hist = parseTripHistory(st.trip_history_m);
    bool hchanged = false;
    for (int i = 0; i < 6; i++) {
        std::string v = (i < static_cast<int>(hist.size())) ? formatDistanceGrouped(hist[i]) : "";
        if (v != h->history[i]) { h->history[i] = v; hchanged = true; }
    }
    if (hchanged) markDirty(h->historyArea);
}
