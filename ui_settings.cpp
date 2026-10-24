#include "ui_settings.h"
#include "callbacks.h"
#include "calculations.h"
#include "config_file.h"
#include "nero_frame.h"
#include "rally_state.h"
#include "webserver/qr_display.h"
#include <cstdio>
#include <string>
#include <vector>

namespace {

constexpr int COL_W = 423, ROW_H = 44, HEAD_H = 26;

enum Setting {
    BEEP_NAV, BEEP_TIMING, TONE_ON, TONE_BEHIND, TONE_AHEAD,
    UNITS, STAGE_LOG, SENSOR_LOG, FORCE_SINGLE
};

struct SegCtl {
    AppData* data;
    Setting id;
    GtkWidget* btn[2];
};

void addClass(GtkWidget* w, const char* c) { gtk_style_context_add_class(gtk_widget_get_style_context(w), c); }

void setLit(GtkWidget* k, bool lit) {
    GtkStyleContext* c = gtk_widget_get_style_context(k);
    if (lit) gtk_style_context_add_class(c, "key-lit");
    else gtk_style_context_remove_class(c, "key-lit");
}

// idx 0 / 1 is the button pressed: off/on, kph/mph, or tone type 1/2.
void apply(AppData* d, Setting id, int idx) {
    RallyState& s = *d->state;
    switch (id) {
    case BEEP_NAV:
        s.beep_navigation_mode = (idx == 1);
        if (idx == 1) d->beepCursorsStale = true;   // a mode that was off never advanced its cursor
        break;
    case BEEP_TIMING:
        s.beep_timing_mode = (idx == 1);
        if (idx == 1) d->beepCursorsStale = true;
        break;
    case TONE_ON:     s.tone_enabled = (idx == 1); break;
    case TONE_BEHIND: s.tone_type_behind = idx + 1; break;
    case TONE_AHEAD:  s.tone_type_ahead = idx + 1; break;
    case UNITS:
        // The unit toggle (on_unit_toggle) flips the stored unit and keeps
        // the driver display in step; only press it when a change is wanted.
        if (s.units != (idx == 1) && d->unitToggleBtn) gtk_button_clicked(d->unitToggleBtn);
        break;
    case STAGE_LOG:   s.stage_logging = (idx == 1); break;
    case SENSOR_LOG:  s.debug_logs_enabled = (idx == 1); setDebugLogging(d, s.debug_logs_enabled); break;
    case FORCE_SINGLE:
        on_force_single_display_toggle(nullptr, idx == 1, d);   // saves itself; applies at next start
        break;
    }
    // Beep Assist is on when either mode is: there is no separate master.
    s.beep_assist_enabled = s.beep_navigation_mode || s.beep_timing_mode;
    ConfigFile::save(s);
}

// Every two-key selector built, so the page can follow changes made from the phone.
std::vector<SegCtl*>& allCtls() { static std::vector<SegCtl*> v; return v; }

int stateIndex(const RallyState& s, Setting id) {
    switch (id) {
    case BEEP_NAV:     return s.beep_navigation_mode ? 1 : 0;
    case BEEP_TIMING:  return s.beep_timing_mode ? 1 : 0;
    case TONE_ON:      return s.tone_enabled ? 1 : 0;
    case TONE_BEHIND:  return s.tone_type_behind == 2 ? 1 : 0;
    case TONE_AHEAD:   return s.tone_type_ahead == 2 ? 1 : 0;
    case UNITS:        return s.units ? 1 : 0;
    case STAGE_LOG:    return s.stage_logging ? 1 : 0;
    case SENSOR_LOG:   return s.debug_logs_enabled ? 1 : 0;
    case FORCE_SINGLE: return s.force_single_display ? 1 : 0;
    }
    return 0;
}

void onSeg(GtkWidget* w, gpointer) {
    SegCtl* c = static_cast<SegCtl*>(g_object_get_data(G_OBJECT(w), "ctl"));
    const int idx = (w == c->btn[1]) ? 1 : 0;
    setLit(c->btn[0], idx == 0);
    setLit(c->btn[1], idx == 1);
    apply(c->data, c->id, idx);
}

// A two-key selector; `active` is 0 or 1.
GtkWidget* seg(AppData* d, Setting id, const char* a, const char* b, int active) {
    SegCtl* c = new SegCtl{d, id, {nullptr, nullptr}};
    allCtls().push_back(c);
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    const char* labels[2] = {a, b};
    for (int i = 0; i < 2; i++) {
        GtkWidget* k = gtk_button_new_with_label(labels[i]);
        gtk_widget_set_size_request(k, 70, 40);
        addClass(k, "dist-heading");
        addClass(k, "key-small");
        g_object_set_data(G_OBJECT(k), "ctl", c);
        g_signal_connect(k, "clicked", G_CALLBACK(onSeg), nullptr);
        setLit(k, i == active);
        gtk_box_pack_start(GTK_BOX(box), k, FALSE, FALSE, 0);
        c->btn[i] = k;
    }
    return box;
}

GtkWidget* row(GtkWidget* col, const char* name) {
    GtkWidget* r = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_size_request(r, COL_W, ROW_H);
    GtkWidget* l = gtk_label_new(name);
    addClass(l, "set-name");
    gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
    gtk_box_pack_start(GTK_BOX(r), l, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(col), r, FALSE, FALSE, 0);
    return r;
}

void advanceEntry(AppData* d, GtkWidget* r, int mode, double stored) {
    GtkEntry* e = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_alignment(e, 1.0f);
    gtk_entry_set_width_chars(e, 1);
    gtk_entry_set_max_width_chars(e, 1);
    gtk_widget_set_size_request(GTK_WIDGET(e), 96, 40);
    addClass(GTK_WIDGET(e), "set-entry");
    if (stored > 0.0) {
        char b[32];
        snprintf(b, sizeof(b), "%.0f", stored);
        gtk_entry_set_text(e, b);
    }
    g_object_set_data(G_OBJECT(e), "mode", GINT_TO_POINTER(mode));
    g_signal_connect(e, "focus-in-event", G_CALLBACK(on_entry_focus), d);
    g_signal_connect(e, "changed", G_CALLBACK(on_beep_advance_changed), d);
    gtk_box_pack_start(GTK_BOX(r), GTK_WIDGET(e), FALSE, FALSE, 0);
    GtkWidget* u = gtk_label_new(mode == 1 ? "s" : "m");
    addClass(u, "dist-unit");
    gtk_widget_set_size_request(u, 20, -1);
    gtk_box_pack_start(GTK_BOX(r), u, FALSE, FALSE, 0);
    if (mode == 1) d->beepAdvanceSecondsEntry = e; else d->beepAdvanceMetresEntry = e;
}

}  // namespace

GtkWidget* createSettingsScreen(AppData* d) {
    RallyState& s = *d->state;
    GtkWidget* screen = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(screen), 5);

    // ── left: BEEP ASSIST & TONE ──
    GtkWidget* left = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_size_request(left, COL_W, -1);
    gtk_box_pack_start(GTK_BOX(screen), left, FALSE, FALSE, 0);
    GtkWidget* h1 = gtk_label_new("BEEP ASSIST & TONE");
    addClass(h1, "seg-head");
    gtk_label_set_xalign(GTK_LABEL(h1), 0.0f);
    gtk_widget_set_size_request(h1, COL_W, HEAD_H);
    gtk_box_pack_start(GTK_BOX(left), h1, FALSE, FALSE, 0);

    gtk_box_pack_end(GTK_BOX(row(left, "Beep navigation")), seg(d, BEEP_NAV, "off", "on", s.beep_navigation_mode ? 1 : 0), FALSE, FALSE, 0);
    advanceEntry(d, row(left, "Navigation advance"), 0, s.beep_advance_m);
    gtk_box_pack_end(GTK_BOX(row(left, "Beep timing")), seg(d, BEEP_TIMING, "off", "on", s.beep_timing_mode ? 1 : 0), FALSE, FALSE, 0);
    advanceEntry(d, row(left, "Timing advance"), 1, s.beep_advance_s);
    gtk_box_pack_end(GTK_BOX(row(left, "Ahead/Behind Tone")), seg(d, TONE_ON, "off", "on", s.tone_enabled ? 1 : 0), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(row(left, "Tone when behind")), seg(d, TONE_BEHIND, "1", "2", s.tone_type_behind == 2 ? 1 : 0), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(row(left, "Tone when ahead")), seg(d, TONE_AHEAD, "1", "2", s.tone_type_ahead == 2 ? 1 : 0), FALSE, FALSE, 0);

    // ── right: SYSTEM, with the version above the speed-units key ──
    GtkWidget* right = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_size_request(right, COL_W, -1);
    gtk_widget_set_margin_start(right, 100);
    gtk_box_pack_start(GTK_BOX(screen), right, FALSE, FALSE, 0);
    GtkWidget* head = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_size_request(head, COL_W, HEAD_H);
    GtkWidget* h2 = gtk_label_new("SYSTEM");
    addClass(h2, "seg-head");
    gtk_label_set_xalign(GTK_LABEL(h2), 0.0f);
    gtk_box_pack_start(GTK_BOX(head), h2, TRUE, TRUE, 0);
    GtkWidget* ver = gtk_label_new("Nero2.0");
    addClass(ver, "seg-small");
    gtk_widget_set_size_request(ver, 140, -1);
    gtk_label_set_xalign(GTK_LABEL(ver), 0.0f);
    gtk_box_pack_end(GTK_BOX(head), ver, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), head, FALSE, FALSE, 0);

    gtk_box_pack_end(GTK_BOX(row(right, "Speed units")), seg(d, UNITS, "kph", "mph", s.units ? 1 : 0), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(row(right, "Stage data logging")), seg(d, STAGE_LOG, "off", "on", s.stage_logging ? 1 : 0), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(row(right, "Sensor logging")), seg(d, SENSOR_LOG, "off", "on", s.debug_logs_enabled ? 1 : 0), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(row(right, "Force single display")), seg(d, FORCE_SINGLE, "off", "on", s.force_single_display ? 1 : 0), FALSE, FALSE, 0);

    d->settingsInfoLabel = GTK_LABEL(gtk_label_new(""));
    addClass(GTK_WIDGET(d->settingsInfoLabel), "set-name");
    gtk_label_set_xalign(d->settingsInfoLabel, 0.0f);
    gtk_widget_set_size_request(GTK_WIDGET(d->settingsInfoLabel), COL_W, ROW_H);
    gtk_box_pack_start(GTK_BOX(right), GTK_WIDGET(d->settingsInfoLabel), FALSE, FALSE, 0);

    // Wi-Fi link: just the address and its QR code (no hotspot switch).
    GtkWidget* wifi = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_size_request(wifi, COL_W, 90);
    GtkWidget* wtext = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* wname = gtk_label_new("Wi-Fi phone link");
    addClass(wname, "set-name");
    gtk_label_set_xalign(GTK_LABEL(wname), 0.0f);
    d->webUrlLabel = GTK_LABEL(gtk_label_new(""));
    addClass(GTK_WIDGET(d->webUrlLabel), "seg-head");
    gtk_label_set_xalign(d->webUrlLabel, 0.0f);
    gtk_box_pack_start(GTK_BOX(wtext), wname, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(wtext), GTK_WIDGET(d->webUrlLabel), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(wifi), wtext, TRUE, TRUE, 0);
    d->webQrArea = gtk_drawing_area_new();
    gtk_widget_set_size_request(d->webQrArea, 80, 80);
    g_signal_connect(d->webQrArea, "draw", G_CALLBACK(on_qr_draw), d);
    gtk_box_pack_end(GTK_BOX(wifi), d->webQrArea, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(right), wifi, FALSE, FALSE, 0);

    // ── keypad and menu key, far right ──
    GtkWidget* kp = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(kp, nero::KEYPAD_COL_W, -1);
    gtk_box_pack_end(GTK_BOX(screen), kp, FALSE, FALSE, 0);
    d->numericKeypad = createNeroKeypad(d);
    gtk_box_pack_start(GTK_BOX(kp), d->numericKeypad, FALSE, FALSE, 0);
    GtkWidget* menu = createMenuKey(d);
    gtk_widget_set_halign(menu, GTK_ALIGN_END);
    gtk_box_pack_end(GTK_BOX(kp), menu, FALSE, FALSE, 0);

    d->settingsScreen = screen;
    return screen;
}

void updateSettingsDisplay(AppData* d) {
    if (!d->settingsInfoLabel) return;
    for (SegCtl* c : allCtls()) {
        const int idx = stateIndex(*d->state, c->id);
        setLit(c->btn[0], idx == 0);
        setLit(c->btn[1], idx == 1);
    }
    std::string info = std::string("Screen ") + gtk_label_get_text(d->updatesPerSecLabel)
                     + "   " + gtk_label_get_text(d->cpuTempLabel);
    gtk_label_set_text(d->settingsInfoLabel, info.c_str());
    updateWebLinkDisplay(d);
}
