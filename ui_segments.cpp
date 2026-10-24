#include "ui_segments.h"
#include "callbacks.h"
#include "calculations.h"
#include "config_file.h"
#include "nero_frame.h"
#include "rally_state.h"
#include "stage_edit.h"
#include "stage_rows.h"
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
constexpr int COLS[11] = {62, 80, 80, 76, 40, 60, 40, 44, 56, 56, 44};
constexpr int COL_GAP = 8;
constexpr int LEFT_W = 1012, LIST_W = 756, STAGE_W = 240, TOP_H = 322, NEWPT_H = 56;

struct RowW {
    GtkWidget* num = nullptr;     // entry on row 1, label below
    GtkWidget* from = nullptr;    // same
    GtkWidget* to = nullptr;
    GtkWidget* dist = nullptr;
    GtkWidget* kph = nullptr;     // entry where chg, label (") otherwise
    GtkWidget* ms = nullptr;
    GtkWidget* time = nullptr;
    GtkWidget* total = nullptr;
};

}  // namespace

struct SegUi {
    GtkWidget* list = nullptr;
    GtkLabel* clock = nullptr;
    GtkWidget* selBtn = nullptr;
    GtkWidget* popover = nullptr;
    GtkWidget* timeEntry = nullptr;
    GtkWidget* distEntry = nullptr;
    GtkWidget* autoCheck = nullptr;
    GtkWidget* addEntry = nullptr;
    std::vector<RowW> rows;
    guint rebuildIdle = 0;
};

namespace {

SegUi* ui(AppData* d) { return static_cast<SegUi*>(g_object_get_data(G_OBJECT(d->stageSetupScreen), "segui")); }

void addClass(GtkWidget* w, const char* c) {
    gtk_style_context_add_class(gtk_widget_get_style_context(w), c);
}

int editSlot(AppData* d) { return stageEditSlot(d); }
RallyState::MemorySlot& slotOf(AppData* d) { return d->state->memory_slots[editSlot(d) - 1]; }

std::string metres(double m) { return formatDistanceGrouped(static_cast<long>(std::lround(m))); }

void commitStage(AppData* d) { stageEditCommit(d); }

// Text and numbers of every row, in place (no widget is created or destroyed,
// so an entry that is being typed into is never pulled from under the finger).
void updateRowText(AppData* d) {
    SegUi* u = ui(d);
    RallyState::MemorySlot& slot = slotOf(d);
    std::vector<StageRow> rows = buildStageRows(d->state->segments, slot.first_number, slot.first_from_m);
    for (size_t i = 0; i < rows.size() && i < u->rows.size(); i++) {
        RowW& w = u->rows[i];
        const StageRow& r = rows[i];
        auto setText = [](GtkWidget* wd, const std::string& t) {
            if (!wd) return;
            if (GTK_IS_ENTRY(wd)) {
                if (!gtk_widget_has_focus(wd)) gtk_entry_set_text(GTK_ENTRY(wd), t.c_str());
            } else {
                gtk_label_set_text(GTK_LABEL(wd), t.c_str());
            }
        };
        setText(w.num, std::to_string(r.number));
        setText(w.from, metres(r.from_m));
        setText(w.to, metres(r.to_m));
        setText(w.dist, metres(r.dist_m));
        char b[24];
        snprintf(b, sizeof(b), "%g", r.kph);
        if (r.chg) setText(w.kph, r.kph > 0 ? b : "");
        setText(w.ms, r.chg && r.kph > 0 ? std::to_string(r.ms) : "");
        setText(w.time, formatMmSs(r.time_s));
        setText(w.total, formatMmSs(r.total_s));
    }
}

void segmentsCommitImpl(AppData* d);

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

gboolean onEntryFocusIn(GtkWidget* w, GdkEvent* e, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    // Tapping another field saves the one being typed in.
    if (d->activeEntry && d->activeEntry != GTK_ENTRY(w)) segmentsCommitImpl(d);
    return on_entry_focus(w, e, d);
}

void onEntryActivate(GtkWidget*, gpointer p) { segmentsCommitImpl(static_cast<AppData*>(p)); }

void onChgToggled(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    segmentsCommitImpl(d);
    size_t row = static_cast<size_t>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "row")));
    stageEditSetChange(d, row, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)));
    scheduleRebuild(d);
}

void onAutoToggled(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    size_t row = static_cast<size_t>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "row")));
    stageEditSetAuto(d, row, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w)));
}

void onDelete(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    segmentsCommitImpl(d);
    size_t row = static_cast<size_t>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "row")));
    stageEditDeleteRow(d, row);
    scheduleRebuild(d);
}

void rebuildNow(AppData* d) {
    SegUi* u = ui(d);
    // The active entry may be about to be destroyed.
    d->activeEntry = nullptr;
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
        auto makeLabel = [&](int col, bool right, bool dim) {
            GtkWidget* l = gtk_label_new("");
            gtk_label_set_xalign(GTK_LABEL(l), right ? 1.0f : 0.5f);
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
            put(e, col);
            return e;
        };

        w.num = (i == 0) ? makeEntry(0, "num") : makeLabel(0, true, false);
        w.from = (i == 0) ? makeEntry(1, "from") : makeLabel(1, true, false);
        w.to = makeEntry(2, "to");
        w.dist = makeLabel(3, true, false);

        GtkWidget* chg = gtk_check_button_new();
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(chg), rows[i].chg);
        if (i == 0) gtk_widget_set_sensitive(chg, FALSE);
        g_object_set_data(G_OBJECT(chg), "row", GINT_TO_POINTER(static_cast<int>(i)));
        g_signal_connect(chg, "toggled", G_CALLBACK(onChgToggled), d);
        gtk_widget_set_halign(chg, GTK_ALIGN_CENTER);
        put(chg, 4);

        if (rows[i].chg) {
            w.kph = makeEntry(5, "kph");
        } else {
            w.kph = makeLabel(5, true, true);
            gtk_label_set_text(GTK_LABEL(w.kph), "\"");
        }
        w.ms = makeLabel(6, true, true);

        if (i == 0) {
            // Row 1 has no auto (the stage starts there): a greyed box keeps the
            // columns after it in line with the rows below.
            GtkWidget* au0 = gtk_check_button_new();
            gtk_widget_set_sensitive(au0, FALSE);
            gtk_widget_set_halign(au0, GTK_ALIGN_CENTER);
            put(au0, 7);
        }
        if (i >= 1) {
            GtkWidget* au = gtk_check_button_new();
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(au), rows[i].auto_on);
            gtk_widget_set_sensitive(au, rows[i].auto_available);
            g_object_set_data(G_OBJECT(au), "row", GINT_TO_POINTER(static_cast<int>(i)));
            g_signal_connect(au, "toggled", G_CALLBACK(onAutoToggled), d);
            gtk_widget_set_halign(au, GTK_ALIGN_CENTER);
            put(au, 7);
        }
        w.time = makeLabel(8, true, true);
        w.total = makeLabel(9, true, true);

        GtkWidget* del = gtk_button_new_with_label("del");
        gtk_widget_set_halign(del, GTK_ALIGN_START);   // keep its own width, never stretched
        g_object_set_data(G_OBJECT(del), "row", GINT_TO_POINTER(static_cast<int>(i)));
        g_signal_connect(del, "clicked", G_CALLBACK(onDelete), d);
        addClass(del, "seg-del");
        put(del, 10);

        gtk_container_add(GTK_CONTAINER(u->list), grid);
        u->rows.push_back(w);
    }
    gtk_widget_show_all(u->list);
    updateRowText(d);
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
                       slot.start_distance_m > 0 ? (formatDistanceGrouped(slot.start_distance_m) + " m").c_str() : "");
}

void selectSlot(AppData* d, int slot_1) {
    if (slot_1 < 1 || slot_1 > RallyState::MAX_MEMORY_SLOTS) return;
    segmentsCommitImpl(d);
    stageEditSelect(d, slot_1);
    loadFields(d);
    rebuildNow(d);
}

void onStageMinus(GtkWidget*, gpointer p) { AppData* d = static_cast<AppData*>(p); selectSlot(d, editSlot(d) - 1); }
void onStagePlus(GtkWidget*, gpointer p) { AppData* d = static_cast<AppData*>(p); selectSlot(d, editSlot(d) + 1); }
void onStageNumber(GtkWidget*, gpointer p) { gtk_popover_popup(GTK_POPOVER(ui(static_cast<AppData*>(p))->popover)); }
void onPickSlot(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    gtk_popover_popdown(GTK_POPOVER(ui(d)->popover));
    selectSlot(d, GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w), "slot")));
}

void onAddDistance(GtkWidget*, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    SegUi* u = ui(d);
    segmentsCommitImpl(d);
    RallyState::MemorySlot& slot = slotOf(d);
    double end = slot.first_from_m;
    for (const Segment& sg : d->state->segments) end += sg.distance_m;
    std::string rest;
    const int added = stageEditAddDistances(d, gtk_entry_get_text(GTK_ENTRY(u->addEntry)), &rest);
    if (added > 0) rebuildNow(d);
    gtk_entry_set_text(GTK_ENTRY(u->addEntry), rest.c_str());
    if (!rest.empty())
        flashNotice(u->addEntry, "Each distance must be larger than the one before ("
                                 + metres(end) + " m so far).");
}

void onClearStage(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
    segmentsCommitImpl(d);
    if (!confirmYesNo(w, "Clear stage", "Clear this stage?")) return;
    stageEditClearStage(d);
    loadFields(d);
    rebuildNow(d);
}

void onClearAll(GtkWidget* w, gpointer p) {
    AppData* d = static_cast<AppData*>(p);
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
void segmentsCommitImpl(AppData* d) {
    SegUi* u = ui(d);
    GtkEntry* e = d->activeEntry;
    if (!u || !e) return;
    const char* kind = static_cast<const char*>(g_object_get_data(G_OBJECT(e), "seg_kind"));
    if (!kind) return;
    const std::string text = gtk_entry_get_text(e);
    RallyState::MemorySlot& slot = slotOf(d);
    const size_t row = static_cast<size_t>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(e), "row")));

    if (!strcmp(kind, "num")) {
        long n = parseMetres(text);
        if (n >= 0) stageEditSetFirst(d, n, -1.0);
    } else if (!strcmp(kind, "from")) {
        long m = parseMetres(text);
        if (m >= 0) stageEditSetFirst(d, -1, static_cast<double>(m));
    } else if (!strcmp(kind, "to")) {
        long m = parseMetres(text);
        if (m >= 0 && !stageEditSetRowTo(d, row, static_cast<double>(m)))
            flashNotice(GTK_WIDGET(e), "That end distance must be past this row's start and short of the next row's end.");
    } else if (!strcmp(kind, "kph")) {
        char* stop = nullptr;
        double v = std::strtod(text.c_str(), &stop);
        if (stop && *stop == '\0' && v >= 0.0 && !text.empty()) stageEditSetSpeed(d, row, v);
    } else if (!strcmp(kind, "start_time")) {
        long t = parseTimeOfDay(text);
        if (t >= 0 || text.empty()) stageEditSetStart(d, t >= 0 ? t : -1, -1);
    } else if (!strcmp(kind, "start_dist")) {
        long m = parseMetres(text);
        if (m >= 0) stageEditSetStart(d, slot.start_time_s, m);
    }
    updateRowText(d);
    // Echo the stored form back into the start fields.
    if (!strcmp(kind, "start_time") || !strcmp(kind, "start_dist")) loadFields(d);
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

void updateSegmentsDisplay(AppData* data) {
    SegUi* u = ui(data);
    if (!u) return;
    gtk_label_set_text(u->clock, formatTime(getRallyTime_ms(*data->state)).c_str());
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
    GtkWidget* left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_size_request(left, LEFT_W, -1);
    gtk_box_pack_start(GTK_BOX(screen), left, FALSE, FALSE, 0);
    GtkWidget* top = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_widget_set_size_request(top, LEFT_W, TOP_H);
    gtk_box_pack_start(GTK_BOX(left), top, FALSE, FALSE, 0);

    // table header and framed rows
    GtkWidget* listCol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_size_request(listCol, LIST_W, TOP_H);
    gtk_box_pack_start(GTK_BOX(top), listCol, FALSE, FALSE, 0);
    GtkWidget* head = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(head), COL_GAP);
    gtk_widget_set_margin_start(head, 7);
    const char* heads[11] = {"#", "from m", "to m", "dist m", "chg", "KPH", "m/s", "auto", "time", "total", ""};
    for (int c = 0; c < 11; c++) {
        GtkWidget* l = gtk_label_new(heads[c]);
        addClass(l, "seg-head");
        gtk_label_set_xalign(GTK_LABEL(l), (c == 4 || c == 7) ? 0.5f : 1.0f);
        gtk_widget_set_size_request(l, COLS[c], -1);
        gtk_grid_attach(GTK_GRID(head), l, c, 0, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(listCol), head, FALSE, FALSE, 0);
    GtkWidget* frame = gtk_frame_new(nullptr);
    addClass(frame, "segment-list-frame");
    gtk_box_pack_start(GTK_BOX(listCol), frame, TRUE, TRUE, 0);
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gtk_container_add(GTK_CONTAINER(frame), scrolled);
    u->list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(u->list), GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scrolled), u->list);
    data->segmentListBox = GTK_LIST_BOX(u->list);

    // stage column (240): clock, selector, start, autostart, clear keys
    GtkWidget* col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_size_request(col, STAGE_W, TOP_H);
    gtk_box_pack_end(GTK_BOX(top), col, FALSE, FALSE, 0);
    u->clock = GTK_LABEL(gtk_label_new("00:00:00"));
    addClass(GTK_WIDGET(u->clock), "seg-clock");
    gtk_widget_set_size_request(GTK_WIDGET(u->clock), STAGE_W, 46);
    gtk_box_pack_start(GTK_BOX(col), GTK_WIDGET(u->clock), FALSE, FALSE, 0);

    GtkWidget* sel = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_halign(sel, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(sel, STAGE_W, 54);
    auto key = [&](const char* t, int w, int h, GCallback cb) {
        GtkWidget* b = gtk_button_new_with_label(t);
        gtk_widget_set_size_request(b, w, h);
        addClass(b, "dist-heading");
        g_signal_connect(b, "clicked", cb, data);
        return b;
    };
    gtk_box_pack_start(GTK_BOX(sel), key("-", 52, 52, G_CALLBACK(onStageMinus)), FALSE, FALSE, 0);
    u->selBtn = key("1", 50, 50, G_CALLBACK(onStageNumber));
    gtk_box_pack_start(GTK_BOX(sel), u->selBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(sel), key("+", 52, 52, G_CALLBACK(onStagePlus)), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(col), sel, FALSE, FALSE, 0);

    u->popover = gtk_popover_new(u->selBtn);
    GtkWidget* pbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(pbox), 8);
    for (int i = 1; i <= 5; i++) {
        GtkWidget* b = key(std::to_string(i).c_str(), 56, 56, G_CALLBACK(onPickSlot));
        g_object_set_data(G_OBJECT(b), "slot", GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(pbox), b, FALSE, FALSE, 0);
    }
    gtk_container_add(GTK_CONTAINER(u->popover), pbox);
    gtk_widget_show_all(pbox);

    GtkWidget* lbl = gtk_label_new("Stage start");
    addClass(lbl, "seg-head");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_box_pack_start(GTK_BOX(col), lbl, FALSE, FALSE, 0);

    auto startRow = [&](const char* text, const char* kind, GtkWidget** entryOut) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget* l = gtk_label_new(text);
        addClass(l, "seg-small");
        gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
        gtk_widget_set_size_request(l, 88, -1);
        GtkWidget* e = gtk_entry_new();
        gtk_entry_set_alignment(GTK_ENTRY(e), 1.0f);
        // width_chars as well as the size request: an entry's natural width
        // otherwise overrides the 144 px and widens the whole column.
        gtk_entry_set_width_chars(GTK_ENTRY(e), 1);
        gtk_entry_set_max_width_chars(GTK_ENTRY(e), 1);
        gtk_widget_set_size_request(e, 144, 44);
        addClass(e, "seg-startentry");
        g_object_set_data(G_OBJECT(e), "seg_kind", const_cast<char*>(kind));
        g_signal_connect(e, "focus-in-event", G_CALLBACK(onEntryFocusIn), data);
        g_signal_connect(e, "activate", G_CALLBACK(onEntryActivate), data);
        gtk_box_pack_start(GTK_BOX(row), l, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), e, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(col), row, FALSE, FALSE, 0);
        *entryOut = e;
    };
    startRow("Time", "start_time", &u->timeEntry);
    startRow("Distance\nat start", "start_dist", &u->distEntry);

    GtkWidget* autoRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* al = gtk_label_new("Autostart");
    addClass(al, "seg-small");
    u->autoCheck = gtk_check_button_new();
    g_signal_connect(u->autoCheck, "toggled", G_CALLBACK(onAutostartToggled), data);
    gtk_box_pack_start(GTK_BOX(autoRow), al, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(autoRow), u->autoCheck, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(col), autoRow, FALSE, FALSE, 0);

    GtkWidget* clr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_valign(clr, GTK_ALIGN_END);
    // Two short lines at 22 px: one 28 px line ("clear stage") is wider than
    // the 117 px half-column and would widen the whole stage column.
    GtkWidget* clrStage = key("clear\nstage", 117, 54, G_CALLBACK(onClearStage));
    GtkWidget* clrAll = key("clear\nall", 117, 54, G_CALLBACK(onClearAll));
    for (GtkWidget* k : {clrStage, clrAll}) {
        addClass(k, "key-small");
        gtk_label_set_justify(GTK_LABEL(gtk_bin_get_child(GTK_BIN(k))), GTK_JUSTIFY_CENTER);
    }
    gtk_box_pack_start(GTK_BOX(clr), clrStage, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(clr), clrAll, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(col), clr, FALSE, FALSE, 0);

    // new point row: add distance + the ';' list
    GtkWidget* newPt = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
    gtk_widget_set_size_request(newPt, LEFT_W, NEWPT_H);
    gtk_box_pack_start(GTK_BOX(left), newPt, FALSE, FALSE, 0);
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
    GtkWidget* menu = createMenuKey(data);
    gtk_widget_set_halign(menu, GTK_ALIGN_END);
    gtk_widget_set_valign(menu, GTK_ALIGN_END);
    gtk_box_pack_end(GTK_BOX(kp), menu, FALSE, FALSE, 0);

    data->activeEntry = nullptr;
    return screen;
}
