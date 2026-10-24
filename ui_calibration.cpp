#include "ui_calibration.h"
#include "callbacks.h"
#include "calculations.h"
#include "nero_frame.h"
#include "rally_state.h"
#include <string>

namespace {

// Content 774 + gap 12 + key column 200 + keypad 240 = 1226 of 1268; the other
// 42 px sit between the keys and the keypad (calibration.html).
constexpr int CONTENT_W = 774, KEYS_W = 200, ROW_H = 56, ROW_GAP = 8;
constexpr int LABEL_W = 380, VALUE_W = 170;

void addClass(GtkWidget* w, const char* c) { gtk_style_context_add_class(gtk_widget_get_style_context(w), c); }

GtkWidget* hrow(GtkWidget* parent) {
    GtkWidget* r = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_size_request(r, CONTENT_W, ROW_H);
    gtk_box_pack_start(GTK_BOX(parent), r, FALSE, FALSE, 0);
    return r;
}

GtkWidget* text(GtkWidget* row, const char* t, int width = -1, bool right = false) {
    GtkWidget* l = gtk_label_new(t);
    addClass(l, "cal-row");
    gtk_label_set_xalign(GTK_LABEL(l), right ? 1.0f : 0.0f);
    if (width > 0) gtk_widget_set_size_request(l, width, -1);
    gtk_box_pack_start(GTK_BOX(row), l, FALSE, FALSE, 0);
    return l;
}

GtkWidget* entry(AppData* d, GtkWidget* row, int width_px, int chars) {
    GtkWidget* e = gtk_entry_new();
    gtk_entry_set_alignment(GTK_ENTRY(e), 1.0f);
    gtk_entry_set_width_chars(GTK_ENTRY(e), chars);
    gtk_entry_set_max_width_chars(GTK_ENTRY(e), chars);
    gtk_widget_set_size_request(e, width_px, 48);
    addClass(e, "cal-entry");
    g_signal_connect(e, "focus-in-event", G_CALLBACK(on_entry_focus), d);
    gtk_box_pack_start(GTK_BOX(row), e, FALSE, FALSE, 0);
    return e;
}

GtkWidget* key(const char* label, int w, int h, GCallback cb, AppData* d, bool tiny = false) {
    GtkWidget* b = gtk_button_new_with_label(label);
    gtk_widget_set_size_request(b, w, h);
    addClass(b, "dist-heading");
    addClass(b, tiny ? "key-tiny" : "key-small");
    gtk_label_set_justify(GTK_LABEL(gtk_bin_get_child(GTK_BIN(b))), GTK_JUSTIFY_CENTER);
    g_signal_connect(b, "clicked", cb, d);
    return b;
}

}  // namespace

GtkWidget* createCalibrationScreen(AppData* d) {
    GtkWidget* screen = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_container_set_border_width(GTK_CONTAINER(screen), 5);

    // Labels the update code writes that this page does not show.
    d->calibrationClockLabel = GTK_LABEL(gtk_label_new(""));
    d->sensorModeLabel = GTK_LABEL(gtk_label_new(""));

    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, ROW_GAP);
    gtk_widget_set_size_request(content, CONTENT_W, -1);
    gtk_box_pack_start(GTK_BOX(screen), content, FALSE, FALSE, 0);

    // 1. calculated distance
    GtkWidget* r1 = hrow(content);
    text(r1, "Calculated distance:", LABEL_W);
    d->totalDistCalLabel = GTK_LABEL(text(r1, "0.0", VALUE_W, true));
    addClass(GTK_WIDGET(d->totalDistCalLabel), "cal-fig");
    text(r1, "m");

    // 2. actual distance
    GtkWidget* r2 = hrow(content);
    text(r2, "Actual distance:", LABEL_W);
    d->rallyDistEntry = GTK_ENTRY(entry(d, r2, VALUE_W, 1));
    text(r2, "m");

    // 3. sensor counts
    GtkWidget* r3 = hrow(content);
    d->totalCountCalLabel = GTK_LABEL(text(r3, ""));
    gtk_label_set_use_markup(d->totalCountCalLabel, TRUE);

    // 4. pulses per km, and the reset entry
    GtkWidget* r4 = hrow(content);
    d->calibrationCurrentLabel = GTK_LABEL(text(r4, ""));
    gtk_label_set_use_markup(d->calibrationCurrentLabel, TRUE);
    d->resetPulsesEntry = GTK_ENTRY(entry(d, r4, 150, 1));   // wide enough for 99999 at 30 px

    // 5. prop / wheel rpm: no key, so it sits below the four keys
    GtkWidget* r5 = hrow(content);
    text(r5, "Prop/Wheel RPM at");
    d->propPulsesEntry = GTK_ENTRY(entry(d, r5, 60, 1));
    gtk_entry_set_max_length(d->propPulsesEntry, 2);
    gtk_entry_set_text(d->propPulsesEntry, std::to_string(d->state->prop_pulses_per_rev).c_str());
    g_signal_connect(d->propPulsesEntry, "changed", G_CALLBACK(on_prop_pulses_changed), d);
    text(r5, "pulses/rev");
    d->propRpmLabel = GTK_LABEL(text(r5, "---", 110, true));
    gtk_label_set_use_markup(d->propRpmLabel, TRUE);
    d->propRpmShown = "---";
    text(r5, "rpm");

    // instructions, small
    GtkWidget* ins = gtk_label_new(
        "INSTRUCTIONS:\n"
        "1. Press zero counters, or skip to step 2 to use the existing counts.\n"
        "2. Enter actual distance covered.  3. Choose sensor 1 or 1&2.  4. Save calibration.");
    addClass(ins, "instruction-label");
    gtk_label_set_xalign(GTK_LABEL(ins), 0.0f);
    gtk_box_pack_start(GTK_BOX(content), ins, FALSE, FALSE, 0);

    // key column: each key level with the line it acts on
    GtkWidget* keys = gtk_box_new(GTK_ORIENTATION_VERTICAL, ROW_GAP);
    gtk_widget_set_size_request(keys, KEYS_W, -1);
    gtk_box_pack_start(GTK_BOX(screen), keys, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(keys), key("zero counters", KEYS_W, ROW_H, G_CALLBACK(on_calibration_start), d), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(keys), key("save\ncalibration", KEYS_W, ROW_H, G_CALLBACK(on_save_calibration), d), FALSE, FALSE, 0);
    GtkWidget* pair = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    d->sensor1Btn = key("using\nsensor 1", 95, ROW_H, G_CALLBACK(on_set_sensor_1), d, true);
    d->sensorBothBtn = key("select\nsensor 1&2", 95, ROW_H, G_CALLBACK(on_set_sensor_both), d, true);
    gtk_box_pack_start(GTK_BOX(pair), d->sensor1Btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(pair), d->sensorBothBtn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(keys), pair, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(keys), key("reset", KEYS_W, ROW_H, G_CALLBACK(on_reset_calibration_pulses), d), FALSE, FALSE, 0);
    updateSensorModeLabel(d);

    // keypad far right, menu under it
    GtkWidget* kp = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_size_request(kp, nero::KEYPAD_COL_W, -1);
    gtk_box_pack_end(GTK_BOX(screen), kp, FALSE, FALSE, 0);
    d->calibrationKeypad = createNeroKeypad(d);
    gtk_box_pack_start(GTK_BOX(kp), d->calibrationKeypad, FALSE, FALSE, 0);
    GtkWidget* menu = createMenuKey(d);
    gtk_widget_set_halign(menu, GTK_ALIGN_END);
    gtk_box_pack_end(GTK_BOX(kp), menu, FALSE, FALSE, 0);

    return screen;
}
