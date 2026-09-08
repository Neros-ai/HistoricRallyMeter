#include "ui_control.h"
#include "sim_counter.h"
#include "rally_state.h"
#include "calculations.h"
#include <sstream>
#include <cstdlib>

// Convert a target speed in km/h to counts/second using the rally's
// calibration (mm per 1000 counts), matching the conversion used everywhere
// else in the app (calculations.cpp).
static double kphToCountsPerSecond(double kph, long calibration) {
    return kphToCountsPerHour(kph, calibration) / 3600.0;
}

// Apply the speed (and resume if paused) to both simulated counters.
void applySpeedToSimCounters(AppData* data, double kph) {
    data->controlSpeedKph = kph;
    // Derived from the calibration as it is NOW -- see resyncSimCounterRate.
    double cps = kphToCountsPerSecond(kph, data->state->calibration);
    if (data->simCounter1) {
        data->simCounter1->setCountsPerSecond(cps);
        data->simCounter1->setPaused(false);
    }
    if (data->simCounter2) {
        data->simCounter2->setCountsPerSecond(cps);
        data->simCounter2->setPaused(false);
    }
}

void resyncSimCounterRate(AppData* data) {
    // The counts-per-second handed to the sim counters is derived from the
    // calibration at the moment the speed was set, so a later calibration
    // change leaves them pulsing at the old rate while every consumer divides
    // by the new one -- the sim silently drives at a different speed than the
    // panel reports. Re-derive, WITHOUT touching paused state: a calibration
    // change must never resume a sim the operator stopped, which is why this
    // is not just a call to applySpeedToSimCounters.
    if (data->controlSpeedKph <= 0.0) return;  // no speed set yet; nothing to resync
    double cps = kphToCountsPerSecond(data->controlSpeedKph, data->state->calibration);
    if (data->simCounter1) data->simCounter1->setCountsPerSecond(cps);
    if (data->simCounter2) data->simCounter2->setCountsPerSecond(cps);
}

static void on_control_speed_clicked(GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    int kph = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "kph"));
    applySpeedToSimCounters(data, static_cast<double>(kph));
    updateControlDisplay(data);
}

static void on_control_start_clicked(GtkWidget*, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    // Default to 25 km/h (the lowest button) if no speed has been chosen yet.
    double kph = data->controlSpeedKph > 0.0 ? data->controlSpeedKph : 25.0;
    applySpeedToSimCounters(data, kph);
    updateControlDisplay(data);
}

static void on_control_stop_clicked(GtkWidget*, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    if (data->simCounter1) data->simCounter1->setPaused(true);
    if (data->simCounter2) data->simCounter2->setPaused(true);
    updateControlDisplay(data);
}

// Apply CSS styling: a green panel, matching the box's bold-white-label
// convention used by ui_copilot.cpp / ui_driver.cpp.
static void applyControlCSS() {
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "window.control-window, window.control-window .background { background-color: #1B5E20; }"
        "window.control-window label { color: #FFFFFF; font-weight: bold; }"
        "window.control-window .control-title { font-size: 26px; }"
        "window.control-window .control-status { font-size: 22px; font-family: monospace; }"
        "window.control-window button { background-color: #2E7D32; color: #FFFFFF; "
        "font-weight: bold; font-size: 22px; border: 2px solid #FFFFFF; background-image: none; }"
        "window.control-window button.active-speed { background-color: #FFFFFF; color: #1B5E20; background-image: none; }"
        "window.control-window button.control-start-button { border-color: #76FF03; }"
        "window.control-window button.control-stop-button { background-color: #B71C1C; background-image: none; }"
        "window.control-window .beep-flash { font-size: 22px; background-color: #FFEB3B; "
        "color: #000000; padding: 6px; }",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

GtkWidget* createControlWindow(AppData* data) {
    applyControlCSS();

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Control Panel");
    gtk_window_set_default_size(GTK_WINDOW(window), 480, 400);
    gtk_style_context_add_class(gtk_widget_get_style_context(window), "control-window");
    // Closing this window must not quit the app (driver/copilot are the
    // primary windows) — just hide it.
    g_signal_connect(window, "delete-event", G_CALLBACK(gtk_widget_hide_on_delete), NULL);

    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 12);
    gtk_container_add(GTK_CONTAINER(window), vbox);

    GtkWidget* title = gtk_label_new("DRIVING CONTROLS (SIM)");
    gtk_style_context_add_class(gtk_widget_get_style_context(title), "control-title");
    gtk_box_pack_start(GTK_BOX(vbox), title, FALSE, FALSE, 0);

    data->controlStatusLabel = GTK_LABEL(gtk_label_new("STOPPED"));
    gtk_style_context_add_class(
        gtk_widget_get_style_context(GTK_WIDGET(data->controlStatusLabel)), "control-status");
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(data->controlStatusLabel), FALSE, FALSE, 0);

    // Speed buttons: 25,30,35,40,45,50 km/h, 3 columns x 2 rows.
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_homogeneous(GTK_GRID(grid), TRUE);
    gtk_grid_set_row_homogeneous(GTK_GRID(grid), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), grid, TRUE, TRUE, 0);

    for (int i = 0; i < 6; i++) {
        int kph = 25 + i * 5;
        GtkWidget* btn = gtk_button_new_with_label(std::to_string(kph).c_str());
        gtk_widget_set_size_request(btn, 100, 70);
        g_object_set_data(G_OBJECT(btn), "kph", GINT_TO_POINTER(kph));
        g_object_set_data(G_OBJECT(btn), "no_click_beep", GINT_TO_POINTER(1));
        g_signal_connect(btn, "clicked", G_CALLBACK(on_control_speed_clicked), data);
        data->controlSpeedButtons[i] = btn;
        gtk_grid_attach(GTK_GRID(grid), btn, i % 3, i / 3, 1, 1);
    }

    // Start / Stop row.
    GtkWidget* hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

    data->controlStartBtn = gtk_button_new_with_label("START");
    gtk_style_context_add_class(gtk_widget_get_style_context(data->controlStartBtn), "control-start-button");
    gtk_widget_set_size_request(data->controlStartBtn, -1, 70);
    g_object_set_data(G_OBJECT(data->controlStartBtn), "no_click_beep", GINT_TO_POINTER(1));
    g_signal_connect(data->controlStartBtn, "clicked", G_CALLBACK(on_control_start_clicked), data);
    gtk_box_pack_start(GTK_BOX(hbox), data->controlStartBtn, TRUE, TRUE, 0);

    data->controlStopBtn = gtk_button_new_with_label("STOP");
    gtk_style_context_add_class(gtk_widget_get_style_context(data->controlStopBtn), "control-stop-button");
    gtk_widget_set_size_request(data->controlStopBtn, -1, 70);
    g_object_set_data(G_OBJECT(data->controlStopBtn), "no_click_beep", GINT_TO_POINTER(1));
    g_signal_connect(data->controlStopBtn, "clicked", G_CALLBACK(on_control_stop_clicked), data);
    gtk_box_pack_start(GTK_BOX(hbox), data->controlStopBtn, TRUE, TRUE, 0);

    // Stands in for the beep sound, which the sandbox can't play (no ALSA
    // device) -- blank until a navigation/timing beep fires, then flashes.
    data->beepFlashLabel = GTK_LABEL(gtk_label_new(""));
    gtk_style_context_add_class(
        gtk_widget_get_style_context(GTK_WIDGET(data->beepFlashLabel)), "beep-flash");
    gtk_widget_set_no_show_all(GTK_WIDGET(data->beepFlashLabel), TRUE);
    gtk_box_pack_start(GTK_BOX(vbox), GTK_WIDGET(data->beepFlashLabel), FALSE, FALSE, 0);

    updateControlDisplay(data);
    return window;
}

void flashBeepWarning(AppData* data, bool navigation_fired) {
    if (!data->beepFlashLabel) return;

    // Navigation and timing can both be due on the same tick, in which case
    // fireBeepAssist() is called twice in a row. Showing both matters: the
    // label used to be overwritten instantly by the second call (so the
    // navigation flash never appeared at all) and the FIRST call's timeout
    // then hid the label 4s later while the second flash was still meant to
    // be up. One label line naming both, and one timer, fixes both halves.
    if (data->beepFlashTimer) {
        // Still showing from a beep moments ago -- name both rather than
        // replacing what the operator may not have read yet.
        gtk_label_set_text(data->beepFlashLabel, "BEEP: NAVIGATION + TIMING");
        g_source_remove(data->beepFlashTimer);
    } else {
        gtk_label_set_text(data->beepFlashLabel,
            navigation_fired ? "BEEP: NAVIGATION" : "BEEP: TIMING");
    }
    gtk_widget_show(GTK_WIDGET(data->beepFlashLabel));

    // Clears itself rather than the next beep overwriting it, so a single
    // beep during a quiet stretch is still visibly transient. 4s, not 1.5s:
    // an operator watching the sandbox manually needs time to actually
    // notice it, not just a log-scale confirmation.
    data->beepFlashTimer = g_timeout_add(4000, [](gpointer d) -> gboolean {
        AppData* app_data = static_cast<AppData*>(d);
        app_data->beepFlashTimer = 0;
        if (app_data->beepFlashLabel) gtk_widget_hide(GTK_WIDGET(app_data->beepFlashLabel));
        return G_SOURCE_REMOVE;
    }, data);
}

void updateControlDisplay(AppData* data) {
    if (!data->controlStatusLabel) return;

    bool running = data->simCounter1 && !data->simCounter1->isPaused();

    std::ostringstream status;
    if (running) {
        // Pulses/km alongside the speed: the sim generates its pulses from
        // this calibration (applySpeedToSimCounters), so a mismatch between
        // "the speed I asked for" and "the calibration actually driving the
        // counters" is exactly the kind of thing worth seeing at a glance
        // here rather than only on the Calibration screen.
        long pulses_per_km = static_cast<long>(pulsesPerKm(data->state->calibration) + 0.5);
        status << static_cast<int>(data->controlSpeedKph) << " km/h at "
               << pulses_per_km << " pulses per KM";
    } else {
        status << "STOPPED";
    }
    gtk_label_set_text(data->controlStatusLabel, status.str().c_str());

    for (int i = 0; i < 6; i++) {
        GtkWidget* btn = data->controlSpeedButtons[i];
        if (!btn) continue;
        int kph = 25 + i * 5;
        GtkStyleContext* ctx = gtk_widget_get_style_context(btn);
        if (running && kph == static_cast<int>(data->controlSpeedKph)) {
            gtk_style_context_add_class(ctx, "active-speed");
        } else {
            gtk_style_context_remove_class(ctx, "active-speed");
        }
    }
}
