#include <algorithm>
#include "ui_segments.h"
#include "stage_store.h"
#include "callbacks.h"
#include "calculations.h"
#include "config_file.h"
#include "nero_frame.h"
#include "rally_state.h"
#include "stage_edit.h"
#include "sector.h"
#include "sector_edit.h"
#include "stage_rows.h"
#include "stage_file.h"
#include <fstream>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace {

// Column widths from segments.html: 50 86 86 86 40 60 40 44 60 60 44 with 8 px
// gaps = 656 + 80 = 736, inside the 756 px list.
// Narrowed a little from the mockup's 50 86 86 86 40 60 40 44 60 60 44 so the
// number entry (3 digits) and the scroll bar both fit inside the 756 px list.
// from m, to m and dist m are wide enough for "99,999" in an entry (92 / 92 / 84);
// the 3-digit # column gave up a little to pay for it.
// RB-SEG-17, the approved shared layout (segments-shared.html): box, chg, auto, from, to,
// dist, KPH, dur, elapsed, del = 737 + 9 gaps x 5 = 782, plus the frame's 14 = 796.
constexpr int COLS[10] = {52, 64, 40, 96, 93, 88, 76, 103, 103, 36};
constexpr int COL_GAP = 5;
// The stage column is 200 px (was 240); the 40 px it gave up widen the table.
constexpr int LEFT_W = 980, NEWPT_H = nero::MENU_KEY_H;
// The left side is as deep as keypad + menu, so the add row sits on the menu line.
constexpr int TOP_H = nero::KEYPAD_H + nero::KEYPAD_MENU_GAP + nero::MENU_KEY_H - 10 - NEWPT_H;   // 308

struct RowW {
    GtkWidget* num = nullptr;     // box: an entry on every row
    GtkWidget* from = nullptr;    // entry on row 1, label below
    GtkWidget* to = nullptr;
    GtkWidget* dist = nullptr;
    GtkWidget* kph = nullptr;     // entry where chg, label (") otherwise
    GtkWidget* time = nullptr;    // dur
    GtkWidget* total = nullptr;   // elapsed
    GtkWidget* grid = nullptr;    // the row, for the current-row outline
    GtkWidget* tag = nullptr;     // italian: the type tag (tap to step)
    GtkWidget* ref = nullptr;     // italian: the time card number
};

}  // namespace

struct SegUi {
    GtkWidget* list = nullptr;
    GtkLabel* clock = nullptr;
    GtkWidget* selBtn = nullptr;
    GtkWidget* timeEntry = nullptr;
    GtkWidget* distEntry = nullptr;
    GtkWidget* autoCheck = nullptr;
    GtkWidget* addEntry = nullptr;
    std::vector<RowW> rows;
    guint rebuildIdle = 0;
    guint autoCommitTimer = 0;   // saves the entry being typed in after a pause
    GtkWidget* scrolled = nullptr;
    GtkWidget* fromToHead = nullptr;   // "from -m- to" / "from -km- to"
    int curRow = -1;             // row outlined as the one being driven
    GtkWidget* headLbl[10] = {};     // column headings (greek and italian words differ)
    GtkWidget* distCaption = nullptr;   // greek: "Distance at start" caption and entry
    GtkWidget* autoRow = nullptr;       // greek: the Autostart tick row
    GtkWidget* durCaption = nullptr;    // italian: "Duration" caption, entry and the faint average
    GtkWidget* durEntry = nullptr;
    GtkWidget* avgLabel = nullptr;
    int builtType = -1;              // the rally type the rows were built for
    GtkWidget* modeBadge = nullptr;  // the rally mode's flag with "mode" on it
    int badgeType = -1;              // the rally type the badge was drawn for
    GtkWidget* uploadBtn = nullptr;
};

// The mode badge (owner, 2026-10-31): a 96 x 24 key-shaped tile, not pressable,
// whose face is the flag of the rally mode (greek: Greece, surrey: the Union Jack,
// italian: Italy) with "mode" in white, outlined in black, across it.
static gboolean drawModeBadge(GtkWidget* w, cairo_t* cr, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    const double W = gtk_widget_get_allocated_width(w), H = gtk_widget_get_allocated_height(w);
    auto rect = [cr](double x, double y, double rw, double rh, double r, double g, double b) {
        cairo_set_source_rgb(cr, r, g, b);
        cairo_rectangle(cr, x, y, rw, rh);
        cairo_fill(cr);
    };
    cairo_save(cr);
    switch (data->state->rally_type) {
    case 2:   // Italy: green, white, red
        rect(0, 0, W / 3, H, 0x00 / 255.0, 0x92 / 255.0, 0x46 / 255.0);
        rect(W / 3, 0, W / 3, H, 1, 1, 1);
        rect(2 * W / 3, 0, W - 2 * W / 3, H, 0xce / 255.0, 0x2b / 255.0, 0x37 / 255.0);
        break;
    case 1: {   // Union Jack, drawn on a 60 x 30 grid
        cairo_scale(cr, W / 60.0, H / 30.0);
        rect(0, 0, 60, 30, 0x01 / 255.0, 0x21 / 255.0, 0x69 / 255.0);
        cairo_move_to(cr, 0, 0); cairo_line_to(cr, 60, 30);
        cairo_move_to(cr, 60, 0); cairo_line_to(cr, 0, 30);
        cairo_set_source_rgb(cr, 1, 1, 1); cairo_set_line_width(cr, 6); cairo_stroke(cr);
        cairo_move_to(cr, 0, 0); cairo_line_to(cr, 60, 30);
        cairo_move_to(cr, 60, 0); cairo_line_to(cr, 0, 30);
        cairo_set_source_rgb(cr, 0xc8 / 255.0, 0x10 / 255.0, 0x2e / 255.0); cairo_set_line_width(cr, 2); cairo_stroke(cr);
        rect(25, 0, 10, 30, 1, 1, 1);
        rect(0, 10, 60, 10, 1, 1, 1);
        rect(27, 0, 6, 30, 0xc8 / 255.0, 0x10 / 255.0, 0x2e / 255.0);
        rect(0, 12, 60, 6, 0xc8 / 255.0, 0x10 / 255.0, 0x2e / 255.0);
        break;
    }
    default: {   // Greece: nine blue and white stripes, white cross on blue in the corner
        const double blue[3] = {0x0d / 255.0, 0x5e / 255.0, 0xaf / 255.0};
        const double sh = H / 9.0;
        for (int i = 0; i < 9; i++) {
            const bool white = i % 2 == 1;
            rect(0, i * sh, W, sh + 0.5, white ? 1 : blue[0], white ? 1 : blue[1], white ? 1 : blue[2]);
        }
        const double c = 5 * sh;
        rect(0, 0, c, c, blue[0], blue[1], blue[2]);
        rect(2 * sh, 0, sh, c, 1, 1, 1);
        rect(0, 2 * sh, c, sh, 1, 1, 1);
        break;
    }
    }
    cairo_restore(cr);

    // the key's edge
    cairo_set_source_rgb(cr, 0xbb / 255.0, 0xbb / 255.0, 0xbb / 255.0);
    cairo_set_line_width(cr, 1);
    cairo_rectangle(cr, 0.5, 0.5, W - 1, H - 1);
    cairo_stroke(cr);

    // "mode": black outline, white face
    cairo_select_font_face(cr, "monospace", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 18);
    cairo_text_extents_t ext;
    cairo_text_extents(cr, "mode", &ext);
    cairo_move_to(cr, (W - ext.width) / 2 - ext.x_bearing, (H - ext.height) / 2 - ext.y_bearing);
    cairo_text_path(cr, "mode");
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_set_line_width(cr, 3);
    cairo_stroke_preserve(cr);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_fill(cr);
    return FALSE;
}

namespace {

SegUi* ui(AppData* d) { return static_cast<SegUi*>(g_object_get_data(G_OBJECT(d->stageSetupScreen), "segui")); }

void addClass(GtkWidget* w, const char* c) {
    gtk_style_context_add_class(gtk_widget_get_style_context(w), c);
}

int editSlot(AppData* d) { return stageEditSlot(d); }
RallyState::MemorySlot& slotOf(AppData* d) { return d->state->memory_slots[editSlot(d) - 1]; }

void markCurrentRow(AppData* d, bool scroll);

void commitStage(AppData* d) { stageEditCommit(d); }

// Column headings follow the rally type: greek box | chg | auto, italian row | type | ref.
void applyHeadings(AppData* d) {
    SegUi* u = ui(d);
    const bool it = d->state->rally_type == 2;
    gtk_label_set_text(GTK_LABEL(u->headLbl[0]), it ? "sub" : "box");
    gtk_label_set_text(GTK_LABEL(u->headLbl[1]), it ? "type" : "chg");
    gtk_label_set_text(GTK_LABEL(u->headLbl[2]), it ? "ref" : "auto");
    gtk_label_set_xalign(GTK_LABEL(u->headLbl[2]), it ? 1.0f : 0.5f);
    gtk_label_set_text(GTK_LABEL(u->fromToHead), d->state->distance_km ? "from -km- to" : "from -m- to");
}

// Text and numbers of every row, in place (no widget is created or destroyed,
// so an entry that is being typed into is never pulled from under the finger).
void updateRowText(AppData* d) {
    SegUi* u = ui(d);
    RallyState::MemorySlot& slot = slotOf(d);
    std::vector<StageRow> rows = buildStageRows(d->state->segments, slot.first_number, slot.first_from_m);
    for (size_t i = 0; i < rows.size() && i < u->rows.size(); i++) {
        RowW& w = u->rows[i];
        const StageRow& r = rows[i];
        auto setText = [d](GtkWidget* wd, const std::string& t) {
            if (!wd) return;
            if (GTK_IS_ENTRY(wd)) {
                // The keypad takes keyboard focus off the entry, so "being typed in" is the active entry, not the focused one.
                if (!gtk_widget_has_focus(wd) && GTK_ENTRY(wd) != d->activeEntry) gtk_entry_set_text(GTK_ENTRY(wd), t.c_str());
            } else {
                gtk_label_set_text(GTK_LABEL(wd), t.c_str());
            }
        };
        const bool km = d->state->distance_km;
        if (d->state->rally_type == 2) {
            const Segment& sg = d->state->segments[i];
            char kb[24];
            snprintf(kb, sizeof(kb), "%.2f", sg.target_speed_kph);
            setText(w.num, std::to_string(i + 1));
            setText(w.ref, (sg.type == SEG_NAVI || sg.type == SEG_LEG) ? "" : std::to_string(sg.ref));
            setText(w.from, formatDistance(r.from_m, km));
            setText(w.to, formatDistance(r.to_m, km));
            setText(w.dist, formatDistance(r.dist_m, km));
            setText(w.kph, sg.target_speed_kph > 0 ? kb : "");
            setText(w.time, sg.type == SEG_PC ? formatMmSs(sg.pc_duration_s) : formatHMmSs(r.time_s));
            setText(w.total, formatHMmSs(r.total_s));
            continue;
        }
        setText(w.num, std::to_string(r.number));
        setText(w.from, formatDistance(r.from_m, km));
        setText(w.to, formatDistance(r.to_m, km));
        setText(w.dist, formatDistance(r.dist_m, km));
        char b[24];
        snprintf(b, sizeof(b), "%g", r.kph);
        if (r.chg) setText(w.kph, r.kph > 0 ? b : "");
        setText(w.time, formatHMmSs(r.time_s));
        setText(w.total, formatHMmSs(r.total_s));
    }
}

// quiet: the automatic save after a pause in typing -- no error pop-up (the
// number may be half typed) and no rewriting of the box being typed in.
void segmentsCommitImpl(AppData* d, bool quiet = false);

// The box has no Enter key, so an edit is saved a moment after the last keypress.
constexpr guint AUTO_COMMIT_MS = 1000;
gboolean autoCommitCb(gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    ui(d)->autoCommitTimer = 0;
    segmentsCommitImpl(d, true);
    return G_SOURCE_REMOVE;
}
void onSegEntryChanged(GtkEditable* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    SegUi* u = ui(d);
    if (!u || d->activeEntry != GTK_ENTRY(w)) return;   // set by code, not typed
    if (u->autoCommitTimer) g_source_remove(u->autoCommitTimer);
    u->autoCommitTimer = g_timeout_add(AUTO_COMMIT_MS, autoCommitCb, d);
}

void rebuildNow(AppData* d);
gboolean rebuildIdleCb(gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    ui(d)->rebuildIdle = 0;
    rebuildNow(d);
    return G_SOURCE_REMOVE;
}
// Structural changes rebuild from an idle handler, never from inside the very
// widget's own signal.
void scheduleRebuild(AppData* d) {
    SegUi* u = ui(d);
    if (!u->rebuildIdle) u->rebuildIdle = g_idle_add(rebuildIdleCb, d);
}

// No stage can be edited while one is live: say so, and let nothing change.
bool editRefused(GtkWidget* w, AppData* d) {
    if (!stageEditLocked(*d->state)) return false;
    flashNotice(w, "Stages cannot be edited while a stage is running");
    return true;
}

gboolean onEntryFocusIn(GtkWidget* w, GdkEvent* e, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    if (editRefused(w, d)) return TRUE;   // no keypad, nothing to type into
    // Tapping another field saves the one being typed in.
    if (d->activeEntry && d->activeEntry != GTK_ENTRY(w)) segmentsCommitImpl(d);
    return on_entry_focus(w, e, d);
}

void onEntryActivate(GtkWidget*, gpointer p) { segmentsCommitImpl(static_cast<AppData*>(p)); }

void onChgToggled(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    if (editRefused(w, d)) { scheduleRebuild(d); return; }   // the rebuild puts the tick back
    segmentsCommitImpl(d);
    size_t row = static_cast<size_t>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "row")));
    stageEditSetChange(d, row, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)));
    scheduleRebuild(d);
}

void onAutoToggled(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    if (editRefused(w, d)) { scheduleRebuild(d); return; }
    size_t row = static_cast<size_t>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "row")));
    stageEditSetAuto(d, row, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)));
}

// Tap on an italian row's type tag: step navi -> pc -> ct -> pm -> co.
void onTypeTag(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    if (editRefused(w, d)) return;
    segmentsCommitImpl(d);
    sectorEditStepType(d, static_cast<size_t>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "row"))));
    scheduleRebuild(d);
}

void onDelete(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    if (editRefused(w, d)) return;
    segmentsCommitImpl(d);
    size_t row = static_cast<size_t>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "row")));
    stageEditDeleteRow(d, row);
    scheduleRebuild(d);
}

// The row being driven, when the stage on show is the one running; -1 otherwise.
int drivenRow(AppData* d) {
    const RallyState& s = *d->state;
    if (s.stage_complete || s.segment_current_number < 0) return -1;
    if (s.last_memory_slot != editSlot(d)) return -1;
    if (s.segment_current_number >= static_cast<long>(s.segments.size())) return -1;
    return static_cast<int>(s.segment_current_number);
}

gboolean scrollToCurrentCb(gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    SegUi* u = ui(d);
    if (!u || u->curRow < 0 || u->curRow >= static_cast<int>(u->rows.size())) return G_SOURCE_REMOVE;
    GtkWidget* row = gtk_widget_get_parent(u->rows[u->curRow].grid);   // the list box row
    gint x = 0, y = 0;
    if (row && gtk_widget_translate_coordinates(row, u->list, 0, 0, &x, &y)) {
        GtkAdjustment* a = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(u->scrolled));
        gtk_adjustment_set_value(a, y > 66 ? y - 66 : 0);   // two rows of context above
    }
    return G_SOURCE_REMOVE;
}

// Outline the driven row in cyan; `scroll` also brings it into view (on open / rebuild).
void markCurrentRow(AppData* d, bool scroll) {
    SegUi* u = ui(d);
    const int cur = drivenRow(d);
    if (cur != u->curRow) {
        for (size_t i = 0; i < u->rows.size(); i++) {
            GtkStyleContext* c = gtk_widget_get_style_context(u->rows[i].grid);
            if (static_cast<int>(i) == cur) gtk_style_context_add_class(c, "seg-current");
            else gtk_style_context_remove_class(c, "seg-current");
        }
        u->curRow = cur;
    }
    if (scroll && cur >= 0) g_idle_add(scrollToCurrentCb, d);
}

void rebuildNow(AppData* d) {
    SegUi* u = ui(d);
    // The active entry may be about to be destroyed.
    d->activeEntry = nullptr;
    applyHeadings(d);
    u->builtType = d->state->rally_type;
    GList* kids = gtk_container_get_children(GTK_CONTAINER(u->list));
    for (GList* l = kids; l; l = l->next) gtk_widget_destroy(GTK_WIDGET(l->data));
    g_list_free(kids);
    u->rows.clear();

    RallyState::MemorySlot& slot = slotOf(d);
    std::vector<StageRow> rows = buildStageRows(d->state->segments, slot.first_number, slot.first_from_m);
    for (size_t i = 0; i < rows.size(); i++) {
        GtkWidget* grid = gtk_grid_new();
        gtk_grid_set_column_spacing(GTK_GRID(grid), COL_GAP);
        addClass(grid, "seg-row");
        RowW w;
        auto put = [&](GtkWidget* wd, int col) {
            gtk_widget_set_size_request(wd, COLS[col], 32);
            gtk_grid_attach(GTK_GRID(grid), wd, col, 0, 1, 1);
        };
        // A tick box centred in its column (a check button given the column width draws its box at the left edge).
        auto putCentered = [&](GtkWidget* wd, int col) {
            GtkWidget* cell = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
            gtk_widget_set_size_request(cell, COLS[col], 32);
            gtk_widget_set_halign(wd, GTK_ALIGN_CENTER);
            gtk_widget_set_valign(wd, GTK_ALIGN_CENTER);
            gtk_box_set_center_widget(GTK_BOX(cell), wd);
            gtk_grid_attach(GTK_GRID(grid), cell, col, 0, 1, 1);
        };
        auto makeLabel = [&](int col, bool right, bool dim) {
            GtkWidget* l = gtk_label_new("");
            gtk_label_set_xalign(GTK_LABEL(l), right ? 1.0f : 0.5f);
            // Fixed width: a long figure may not widen its column in this row only
            // (the columns after it would no longer line up with the other rows).
            gtk_label_set_width_chars(GTK_LABEL(l), 1);
            gtk_label_set_max_width_chars(GTK_LABEL(l), 1);
            gtk_label_set_ellipsize(GTK_LABEL(l), PANGO_ELLIPSIZE_START);
            if (dim) addClass(l, "seg-dim");
            put(l, col);
            return l;
        };
        auto makeEntry = [&](int col, const char* kind) {
            GtkWidget* e = gtk_entry_new();
            // 1 char, not 3: an entry's natural width otherwise beats the column's
            // fixed width and widens that column in the rows that have one, so the
            // columns after it no longer line up with the rows that do not.
            gtk_entry_set_width_chars(GTK_ENTRY(e), 1);
            gtk_entry_set_max_width_chars(GTK_ENTRY(e), 1);
            gtk_entry_set_alignment(GTK_ENTRY(e), 1.0f);
            addClass(e, "seg-entry");
            g_object_set_data(G_OBJECT(e), "seg_kind", const_cast<char*>(kind));
            g_object_set_data(G_OBJECT(e), "row", GINT_TO_POINTER(static_cast<int>(i)));
            g_signal_connect(e, "focus-in-event", G_CALLBACK(onEntryFocusIn), d);
            g_signal_connect(e, "activate", G_CALLBACK(onEntryActivate), d);
            g_signal_connect(e, "changed", G_CALLBACK(onSegEntryChanged), d);
            put(e, col);
            return e;
        };

        w.grid = grid;
        if (d->state->rally_type == 2) {
            // Italian row (segments-shared.html): row | type tag | ref | from | to | dist | KPH | dur | elapsed | del.
            // pc: the card time is typed (dur) and the speed worked out; pm: the speed is typed; the rest are worked out.
            const Segment& sg = d->state->segments[i];
            w.num = makeLabel(0, true, false);
            w.tag = gtk_button_new_with_label(sg.type == SEG_LEG ? "-" : typeLabel(sg.type).c_str());   // "-": a greek row not yet typed, tap to type it
            gtk_widget_set_halign(w.tag, GTK_ALIGN_CENTER);
            g_object_set_data(G_OBJECT(w.tag), "row", GINT_TO_POINTER(static_cast<int>(i)));
            g_signal_connect(w.tag, "clicked", G_CALLBACK(onTypeTag), d);
            addClass(w.tag, "seg-tag");
            addClass(w.tag, (std::string("seg-tag-") + typeLabel(sg.type)).c_str());
            put(w.tag, 1);
            const bool noRef = sg.type == SEG_NAVI || sg.type == SEG_LEG;
            w.ref = noRef ? nullptr : makeEntry(2, "ref");
            if (noRef) makeLabel(2, true, true);   // empty, but it holds the column open so the row lines up
            w.from = (i == 0) ? makeEntry(3, "from") : makeLabel(3, true, true);   // greyed: the row above's to
            w.to = makeEntry(4, "to");
            w.dist = makeLabel(5, true, false);
            w.kph = (sg.type == SEG_PM) ? makeEntry(6, "kph") : makeLabel(6, true, true);
            w.time = (sg.type == SEG_PC) ? makeEntry(7, "pcdur") : makeLabel(7, true, true);
            w.total = makeLabel(8, true, true);
        } else {
        w.num = makeEntry(0, "num");

        GtkWidget* chg = gtk_check_button_new();
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chg), rows[i].chg);
        if (i == 0) gtk_widget_set_sensitive(chg, FALSE);
        g_object_set_data(G_OBJECT(chg), "row", GINT_TO_POINTER(static_cast<int>(i)));
        g_signal_connect(chg, "toggled", G_CALLBACK(onChgToggled), d);
        gtk_widget_set_halign(chg, GTK_ALIGN_CENTER);
        putCentered(chg, 1);

        if (i == 0) {
            // Row 1 has no auto (the stage starts there): a greyed box keeps the
            // columns after it in line with the rows below.
            GtkWidget* au0 = gtk_check_button_new();
            gtk_widget_set_sensitive(au0, FALSE);
            gtk_widget_set_halign(au0, GTK_ALIGN_CENTER);
            putCentered(au0, 2);
        }
        if (i >= 1) {
            GtkWidget* au = gtk_check_button_new();
            // Ticked only where it applies: with no speed change the box is greyed and empty, like row 1.
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(au), rows[i].auto_available && rows[i].auto_on);
            gtk_widget_set_sensitive(au, rows[i].auto_available);
            g_object_set_data(G_OBJECT(au), "row", GINT_TO_POINTER(static_cast<int>(i)));
            g_signal_connect(au, "toggled", G_CALLBACK(onAutoToggled), d);
            gtk_widget_set_halign(au, GTK_ALIGN_CENTER);
            putCentered(au, 2);
        }

        w.from = (i == 0) ? makeEntry(3, "from") : makeLabel(3, true, false);
        w.to = makeEntry(4, "to");
        w.dist = makeLabel(5, true, false);
        if (rows[i].chg) {
            w.kph = makeEntry(6, "kph");
        } else {
            w.kph = makeLabel(6, true, true);
            gtk_label_set_text(GTK_LABEL(w.kph), "\"");
        }
        w.time = makeLabel(7, true, true);
        w.total = makeLabel(8, true, true);

        }

        GtkWidget* del = gtk_button_new_with_label("X");
        gtk_widget_set_halign(del, GTK_ALIGN_START);   // keep its own width, never stretched
        g_object_set_data(G_OBJECT(del), "row", GINT_TO_POINTER(static_cast<int>(i)));
        g_signal_connect(del, "clicked", G_CALLBACK(onDelete), d);
        gtk_widget_set_size_request(del, 30, 28);
        addClass(del, "seg-del");
        put(del, 9);

        gtk_container_add(GTK_CONTAINER(u->list), grid);
        u->rows.push_back(w);
    }
    gtk_widget_show_all(u->list);
    updateRowText(d);
    u->curRow = -2;            // force the outline to be drawn for the new rows
    markCurrentRow(d, true);
}

// Fill the stage column from the memory on show.
void loadFields(AppData* d) {
    SegUi* u = ui(d);
    RallyState::MemorySlot& slot = slotOf(d);
    gtk_button_set_label(GTK_BUTTON(u->selBtn), std::to_string(editSlot(d)).c_str());
    char b[24];
    if (slot.start_time_s >= 0) {
        snprintf(b, sizeof(b), "%02ld:%02ld:%02ld", slot.start_time_s / 3600,
                 (slot.start_time_s / 60) % 60, slot.start_time_s % 60);
        gtk_entry_set_text(GTK_ENTRY(u->timeEntry), b);
    } else {
        gtk_entry_set_text(GTK_ENTRY(u->timeEntry), "");
    }
    gtk_entry_set_text(GTK_ENTRY(u->distEntry),
                       slot.start_distance_m > 0
                           ? (formatDistance(static_cast<double>(slot.start_distance_m), d->state->distance_km) +
                              (d->state->distance_km ? " km" : " m")).c_str() : "");
    // Greek shows the distance at start and the Autostart tick; italian shows the sector duration instead.
    const bool it = d->state->rally_type == 2;
    for (GtkWidget* w : {u->distCaption, u->distEntry, u->autoRow}) gtk_widget_set_visible(w, !it);
    for (GtkWidget* w : {u->durCaption, u->durEntry, u->avgLabel}) gtk_widget_set_visible(w, it);
    if (it) {
        if (slot.sector_duration_s > 0)
            gtk_entry_set_text(GTK_ENTRY(u->durEntry), formatHMmSs(static_cast<double>(slot.sector_duration_s)).c_str());
        else gtk_entry_set_text(GTK_ENTRY(u->durEntry), "");
        double m = 0.0;
        for (const Segment& sg : d->state->segments) m += sg.distance_m;
        if (slot.sector_duration_s > 0 && m > 0.0) {
            char ab[32];
            snprintf(ab, sizeof(ab), "Average %.2f", m / 1000.0 / (slot.sector_duration_s / 3600.0));
            gtk_label_set_text(GTK_LABEL(u->avgLabel), ab);
        } else {
            gtk_label_set_text(GTK_LABEL(u->avgLabel), "");
        }
    }
}

void selectSlot(AppData* d, int slot_1) {
    if (slot_1 < 1 || slot_1 > RallyState::MAX_MEMORY_SLOTS) return;
    segmentsCommitImpl(d);
    stageEditSelect(d, slot_1);
    loadFields(d);
    rebuildNow(d);
}

// - / + step to the next programmed stage, skipping gaps (RB-NAV-45); a new
// stage is started by typing its number in Choose Stage (tap the number).
void onStageMinus(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    if (editRefused(w, d)) return;
    const int to = nextProgrammedStage(*d->state, editSlot(d), -1);
    if (to != editSlot(d)) selectSlot(d, to);
}
void onStagePlus(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    if (editRefused(w, d)) return;
    const int to = nextProgrammedStage(*d->state, editSlot(d), +1);
    if (to != editSlot(d)) selectSlot(d, to);
}
void onStageNumber(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    if (editRefused(w, d)) return;
    const int n = chooseStageDialog(w, d, editSlot(d));
    if (n) selectSlot(d, n);   // an empty number opens empty, ready to program
}

void onAddDistance(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    if (editRefused(w, d)) return;
    SegUi* u = ui(d);
    segmentsCommitImpl(d);
    RallyState::MemorySlot& slot = slotOf(d);
    if (d->state->segments.empty()) slot.first_from_m = 0.0;   // as stageEditAddDistances does
    std::string rest, notice;
    const int added = stageEditAddDistances(d, gtk_entry_get_text(GTK_ENTRY(u->addEntry)), &rest, &notice);
    if (added > 0) rebuildNow(d);
    gtk_entry_set_text(GTK_ENTRY(u->addEntry), rest.c_str());
    if (!rest.empty())
        flashNotice(u->addEntry, "Not a distance (it must be more than 0): " + rest);
    else if (!notice.empty())
        flashNotice(u->addEntry, notice);
}

void onClearStage(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    if (editRefused(w, d)) return;
    segmentsCommitImpl(d);
    if (!confirmYesNo(w, "Clear stage", "Clear this stage?")) return;
    stageEditClearStage(d);
    loadFields(d);
    rebuildNow(d);
}

void onClearAll(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    if (editRefused(w, d)) return;
    segmentsCommitImpl(d);
    if (!confirmYesNo(w, "Clear all", "Clear every stage?")) return;
    stageEditClearAll(d);
    loadFields(d);
    rebuildNow(d);
}

// Ticking arms an autostart at the stage's start time today; unticking
// disarms. Typing a time only stores it.
void onAutostartToggled(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    segmentsCommitImpl(d);
    std::string err;
    const bool on = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
    if (!stageEditSetAutostart(d, on, &err)) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), FALSE);
        flashNotice(w, err);
    }
}

}  // namespace

// ── commit of the entry being typed into ─────────────────────────────────────
namespace {
void segmentsCommitImpl(AppData* d, bool quiet) {
    SegUi* u = ui(d);
    GtkEntry* e = d->activeEntry;
    if (!u || !e) return;
    const char* kind = static_cast<const char*>(g_object_get_data(G_OBJECT(e), "seg_kind"));
    if (!kind) return;
    const std::string text = gtk_entry_get_text(e);
    RallyState::MemorySlot& slot = slotOf(d);
    const size_t row = static_cast<size_t>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(e), "row")));

    const bool km = d->state->distance_km;
    if (!strcmp(kind, "num")) {
        long n = parseMetres(text);
        if (n >= 0) stageEditSetBox(d, row, n);
    } else if (!strcmp(kind, "from")) {
        long m = parseDistance(text, km);
        if (m >= 0) stageEditSetFirst(d, -1, static_cast<double>(m));
    } else if (!strcmp(kind, "to")) {
        long m = parseDistance(text, km);
        if (m >= 0 && !stageEditSetRowTo(d, row, static_cast<double>(m)) && !quiet)
            flashNotice(GTK_WIDGET(e), "That end distance must be past this row's start and short of the next row's end.");
    } else if (!strcmp(kind, "kph")) {
        char* stop = nullptr;
        double v = std::strtod(text.c_str(), &stop);
        if (stop && *stop == '\0' && v >= 0.0 && !text.empty()) stageEditSetSpeed(d, row, v);
    } else if (!strcmp(kind, "ref")) {
        long n = parseMetres(text);
        if (n >= 0) sectorEditSetRef(d, row, static_cast<int>(n));
    } else if (!strcmp(kind, "pcdur")) {
        long s = parseDurationSeconds(text);
        if (s > 0) sectorEditSetPcDuration(d, row, static_cast<double>(s));
    } else if (!strcmp(kind, "sector_dur")) {
        long s = parseDurationSeconds(text);
        if (s > 0 || text.empty()) sectorEditSetSectorDuration(d, s > 0 ? s : -1);
    } else if (!strcmp(kind, "start_time")) {
        long t = parseTimeOfDay(text);
        if (t >= 0 || text.empty()) {
            std::string notice;
            stageEditSetStart(d, t >= 0 ? t : -1, -1, &notice);
            if (!notice.empty() && !quiet) flashNotice(GTK_WIDGET(e), notice);
        }
    } else if (!strcmp(kind, "start_dist")) {
        long m = parseDistance(text, d->state->distance_km);   // in the rally's unit, km or m
        if (m >= 0) stageEditSetStart(d, slot.start_time_s, m);
    }
    updateRowText(d);
    // Echo the stored form back into the start fields.
    if (!quiet && (!strcmp(kind, "start_time") || !strcmp(kind, "start_dist"))) loadFields(d);
}
}  // namespace

void segmentsCommitActive(AppData* data) {
    if (!data->stageSetupScreen || !ui(data)) return;
    segmentsCommitImpl(data);
}

void refreshSegmentList(AppData* data) {
    if (!data->stageSetupScreen || !ui(data)) return;
    editSlot(data);
    loadFields(data);
    rebuildNow(data);
}

void onUploadStage(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    if (!d) return;
    if (stageLive(*d->state)) {
        flashNotice(w, "a stage is running: upload after it ends");
        return;
    }
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Upload stage", GTK_WINDOW(gtk_widget_get_toplevel(w)),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "Cancel", GTK_RESPONSE_CANCEL, "Open", GTK_RESPONSE_ACCEPT, nullptr);
    GtkFileFilter* filter = gtk_file_filter_new();
    gtk_file_filter_set_name(filter, "Stage file");
    gtk_file_filter_add_pattern(filter, "*.csv");
    gtk_file_filter_add_pattern(filter, "*.txt");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dialog), filter);
    const int response = gtk_dialog_run(GTK_DIALOG(dialog));
    char* path = response == GTK_RESPONSE_ACCEPT
        ? gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog)) : nullptr;
    gtk_widget_destroy(dialog);
    if (!path) return;
    std::ifstream in(path);
    g_free(path);
    if (!in) { flashNotice(w, "could not read that file"); return; }
    std::string text, line;
    while (std::getline(in, line)) {
        if (text.size() + line.size() + 1 > 64 * 1024) {
            flashNotice(w, "that file is too big");
            return;
        }
        text += line;
        text.push_back('\n');
    }
    const StageFile file = parseStageFile(text);
    const std::string why = uploadRefusal(*d->state, file);
    if (!why.empty()) { flashNotice(w, why); return; }
    const std::vector<int> over = uploadOverwrites(*d->state, file);
    if (!over.empty() && !confirmYesNo(w, "Upload stage", overwriteQuestion(over))) return;
    stageUploadCommit(d, file);
}

void updateSegmentsDisplay(AppData* data) {
    SegUi* u = ui(data);
    if (!u) return;
    gtk_label_set_text(u->clock, formatTime(getRallyTime_ms(*data->state)).c_str());
    // The rally type was changed on Settings: rebuild the rows and the stage column for it.
    if (u->builtType != data->state->rally_type && !data->activeEntry) {
        loadFields(data);
        rebuildNow(data);
    }
    applyHeadings(data);
    updateRowText(data);   // distance units can change on Settings or the phone
    if (u->badgeType != data->state->rally_type) {
        u->badgeType = data->state->rally_type;
        gtk_widget_queue_draw(u->modeBadge);
    }
    markCurrentRow(data, false);
    if (u->uploadBtn) gtk_widget_set_sensitive(u->uploadBtn, !stageLive(*data->state));
    const bool armed = data->state->auto_start_rally_time_s != 0;
    if (armed != static_cast<bool>(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(u->autoCheck)))) {
        g_signal_handlers_block_by_func(u->autoCheck, (gpointer)onAutostartToggled, data);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(u->autoCheck), armed);
        g_signal_handlers_unblock_by_func(u->autoCheck, (gpointer)onAutostartToggled, data);
    }
}

GtkWidget* createStageSetupScreen(AppData* data) {
    SegUi* u = new SegUi();
    GtkWidget* screen = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_container_set_border_width(GTK_CONTAINER(screen), 5);
    g_object_set_data(G_OBJECT(screen), "segui", u);
    data->stageSetupScreen = screen;   // ui() reads it, so it is set before anything is built

    // ── left area: top row (list + stage column) and the add-distance row ──
    GtkWidget* left = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_size_request(left, LEFT_W, -1);
    gtk_box_pack_start(GTK_BOX(screen), left, FALSE, FALSE, 0);

    // The stage column is exactly as wide as the rally clock's text at the top of
    // it ("00:00:00" at 40 px); the table takes the rest of the 1012 px.
    int STAGE_W = 150;
    {
        GtkWidget* probe = gtk_label_new("00:00:00");
        addClass(probe, "seg-clock");
        gint nat = 0;
        gtk_widget_get_preferred_width(probe, nullptr, &nat);
        (void)nat;   // the probe over-reads; the column is set by hand
        g_object_ref_sink(probe);
        g_object_unref(probe);
    }
    const int LIST_W = LEFT_W - 16 - STAGE_W;
    // The table and the add-distance row stack in the left part; the stage
    // column beside them runs the full height, with clear stage at its foot.
    GtkWidget* top = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_size_request(top, LIST_W, TOP_H + 10 + NEWPT_H);
    gtk_box_pack_start(GTK_BOX(left), top, FALSE, FALSE, 0);

    // table header and framed rows
    GtkWidget* listCol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_size_request(listCol, LIST_W, TOP_H);
    gtk_box_pack_start(GTK_BOX(top), listCol, FALSE, FALSE, 0);
    GtkWidget* head = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(head), COL_GAP);
    gtk_widget_set_margin_start(head, 3);
    // box chg auto | from -m- to (over both slots, centred) | dist KPH dur elapsed
    const char* heads[10] = {"box", "chg", "auto", "", "", "dist", "KPH", "dur", "elapsed", ""};
    for (int c = 0; c < 10; c++) {
        if (c == 3 || c == 4) continue;
        GtkWidget* l = gtk_label_new(heads[c]);
        addClass(l, "seg-head");
        gtk_label_set_xalign(GTK_LABEL(l), (c == 1 || c == 2) ? 0.5f : 1.0f);
        gtk_widget_set_size_request(l, COLS[c], -1);
        gtk_grid_attach(GTK_GRID(head), l, c, 0, 1, 1);
        u->headLbl[c] = l;
    }
    u->fromToHead = gtk_label_new(data->state->distance_km ? "from -km- to" : "from -m- to");
    addClass(u->fromToHead, "seg-head");
    gtk_label_set_xalign(GTK_LABEL(u->fromToHead), 0.5f);
    gtk_widget_set_size_request(u->fromToHead, COLS[3] + COL_GAP + COLS[4], -1);
    gtk_grid_attach(GTK_GRID(head), u->fromToHead, 3, 0, 2, 1);
    gtk_box_pack_start(GTK_BOX(listCol), head, FALSE, FALSE, 0);
    GtkWidget* frame = gtk_frame_new(nullptr);
    addClass(frame, "segment-list-frame");
    gtk_box_pack_start(GTK_BOX(listCol), frame, TRUE, TRUE, 0);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    u->scrolled = scrolled;
    gtk_container_add(GTK_CONTAINER(frame), scrolled);
    u->list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(u->list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scrolled), u->list);
    data->segmentListBox = GTK_LIST_BOX(u->list);

    // stage column: clock, selector, start, autostart
    GtkWidget* col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_size_request(col, STAGE_W, TOP_H + 10 + NEWPT_H);
    gtk_box_pack_end(GTK_BOX(left), col, FALSE, FALSE, 0);
    u->clock = GTK_LABEL(gtk_label_new("00:00:00"));
    addClass(GTK_WIDGET(u->clock), "seg-clock");
    gtk_widget_set_size_request(GTK_WIDGET(u->clock), STAGE_W, 42);
    gtk_box_pack_start(GTK_BOX(col), GTK_WIDGET(u->clock), FALSE, FALSE, 0);

    // - 1 + spread edge to edge: three keys, each drawn about 53 px wide (the button adds padding to its 50-52 px request), the rest split into two gaps.
    GtkWidget* sel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_halign(sel, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(sel, 8);           // clear of the rally clock above
    gtk_widget_set_size_request(sel, STAGE_W, 46);
    auto key = [&](const char* t, int w, int h, GCallback cb) {
        GtkWidget* b = gtk_button_new_with_label(t);
        gtk_widget_set_size_request(b, w, h);
        addClass(b, "dist-heading");
        if (cb) g_signal_connect(b, "clicked", cb, data);
        return b;
    };
    gtk_box_pack_start(GTK_BOX(sel), key("-", 44, 44, G_CALLBACK(onStageMinus)), FALSE, FALSE, 0);
    u->selBtn = key("1", 42, 44, G_CALLBACK(onStageNumber));
    gtk_box_pack_start(GTK_BOX(sel), u->selBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sel), key("+", 44, 44, G_CALLBACK(onStagePlus)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(col), sel, FALSE, FALSE, 0);

    GtkWidget* lbl = gtk_label_new("Start time");
    addClass(lbl, "seg-head");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_widget_set_margin_top(lbl, 10);   // clear of the - 1 + row above
    gtk_box_pack_start(GTK_BOX(col), lbl, FALSE, FALSE, 0);

    // An entry right-aligned in the column; `label` (cyan, like the heading) goes on
    // the row above it, or nothing for the time, which the heading already names.
    auto startRow = [&](const char* label, const char* kind, GtkWidget** entryOut, GtkWidget** labelOut = nullptr) {
        if (label) {
            GtkWidget* l = gtk_label_new(label);
            addClass(l, "seg-head");
            gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
            gtk_box_pack_start(GTK_BOX(col), l, FALSE, FALSE, 0);
            if (labelOut) *labelOut = l;
        }
        GtkWidget* e = gtk_entry_new();
        gtk_entry_set_alignment(GTK_ENTRY(e), 1.0f);
        // width_chars as well as the size request: an entry's natural width
        // otherwise overrides the 144 px and widens the whole column.
        gtk_entry_set_width_chars(GTK_ENTRY(e), 1);
        gtk_entry_set_max_width_chars(GTK_ENTRY(e), 1);
        gtk_widget_set_size_request(e, STAGE_W, 38);
        gtk_widget_set_halign(e, GTK_ALIGN_END);
        addClass(e, "seg-startentry");
        g_object_set_data(G_OBJECT(e), "seg_kind", const_cast<char*>(kind));
        g_signal_connect(e, "focus-in-event", G_CALLBACK(onEntryFocusIn), data);
        g_signal_connect(e, "activate", G_CALLBACK(onEntryActivate), data);
        g_signal_connect(e, "changed", G_CALLBACK(onSegEntryChanged), data);
        gtk_box_pack_start(GTK_BOX(col), e, FALSE, FALSE, 0);
        *entryOut = e;
    };
    startRow(nullptr, "start_time", &u->timeEntry);
    startRow("Distance at start", "start_dist", &u->distEntry, &u->distCaption);
    // Italian: the sector's set duration (h:mm:ss) and, faintly, the average speed it implies.
    startRow("Duration", "sector_dur", &u->durEntry, &u->durCaption);
    u->avgLabel = gtk_label_new("");
    addClass(u->avgLabel, "seg-dim");
    addClass(u->avgLabel, "seg-small");   // 18 px: fits the 150 px stage column
    gtk_label_set_xalign(GTK_LABEL(u->avgLabel), 1.0f);
    gtk_box_pack_start(GTK_BOX(col), u->avgLabel, FALSE, FALSE, 0);

    GtkWidget* autoRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    u->autoRow = autoRow;
    GtkWidget* al = gtk_label_new("Autostart");
    addClass(al, "seg-head");   // same 18 px cyan as "Distance at start"
    u->autoCheck = gtk_check_button_new();
    g_signal_connect(u->autoCheck, "toggled", G_CALLBACK(onAutostartToggled), data);
    gtk_box_pack_start(GTK_BOX(autoRow), al, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(autoRow), u->autoCheck, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(col), autoRow, FALSE, FALSE, 0);

    // clear stage and clear all at the foot of the column, 97 wide each, on two lines, as tall as menu.
    const int CLR_W = (STAGE_W - 4) / 2;
    GtkWidget* clrRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, std::max(2, STAGE_W - 2 * CLR_W));
    GtkWidget* clrStage = key("clear\nstage", CLR_W, nero::MENU_KEY_H, G_CALLBACK(onClearStage));
    addClass(clrStage, "seg-clr");
    gtk_label_set_justify(GTK_LABEL(gtk_bin_get_child(GTK_BIN(clrStage))), GTK_JUSTIFY_CENTER);
    GtkWidget* clrAll = key("clear\nall", CLR_W, nero::MENU_KEY_H, G_CALLBACK(onClearAll));
    addClass(clrAll, "seg-clr");
    gtk_label_set_justify(GTK_LABEL(gtk_bin_get_child(GTK_BIN(clrAll))), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(clrRow), clrStage, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(clrRow), clrAll, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(col), clrRow, FALSE, FALSE, 0);
    // The mode badge, centred just above clear stage / clear all.
    u->modeBadge = gtk_drawing_area_new();
    gtk_widget_set_size_request(u->modeBadge, 96, 24);
    gtk_widget_set_halign(u->modeBadge, GTK_ALIGN_CENTER);
    g_signal_connect(u->modeBadge, "draw", G_CALLBACK(drawModeBadge), data);
    gtk_box_pack_end(GTK_BOX(col), u->modeBadge, FALSE, FALSE, 0);

    // new point row: add distance + the ';' list
    GtkWidget* newPt = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_set_size_request(newPt, LIST_W, NEWPT_H);
    gtk_box_pack_start(GTK_BOX(top), newPt, FALSE, FALSE, 0);
    GtkWidget* add = key("add distance", 200, NEWPT_H, G_CALLBACK(onAddDistance));
    addClass(add, "key-small");
    gtk_box_pack_start(GTK_BOX(newPt), add, FALSE, FALSE, 0);
    u->addEntry = gtk_entry_new();
    gtk_widget_set_size_request(u->addEntry, -1, NEWPT_H);
    addClass(u->addEntry, "seg-addentry");
    g_signal_connect(u->addEntry, "focus-in-event", G_CALLBACK(onEntryFocusIn), data);
    gtk_box_pack_start(GTK_BOX(newPt), u->addEntry, TRUE, TRUE, 0);

    // keypad column: keypad, then the menu key under it, right-aligned
    GtkWidget* kp = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(kp, nero::KEYPAD_COL_W, -1);
    gtk_box_pack_start(GTK_BOX(screen), kp, FALSE, FALSE, 0);
    data->numericKeypad = createNeroKeypad(data);
    gtk_box_pack_start(GTK_BOX(kp), data->numericKeypad, FALSE, FALSE, 0);
    // upload stage: the same file the phone loads. Greyed while a stage is running.
    const int UPL_W = nero::KEYPAD_COL_W - nero::MENU_KEY_W - 8;
    GtkWidget* upload = key("upload\nstage", UPL_W, nero::MENU_KEY_H, G_CALLBACK(onUploadStage));
    addClass(upload, "key-small");
    gtk_label_set_justify(GTK_LABEL(gtk_bin_get_child(GTK_BIN(upload))), GTK_JUSTIFY_CENTER);
    u->uploadBtn = upload;
    GtkWidget* menu = createMenuKey(data);
    GtkWidget* menuRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(menuRow), upload, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(menuRow), menu, FALSE, FALSE, 0);
    gtk_widget_set_halign(menuRow, GTK_ALIGN_END);
    gtk_widget_set_valign(menuRow, GTK_ALIGN_END);
    gtk_box_pack_end(GTK_BOX(kp), menuRow, FALSE, FALSE, 0);

    data->activeEntry = nullptr;
    return screen;
}
