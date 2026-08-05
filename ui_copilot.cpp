#include "ui_copilot.h"
#include "ui_driver.h"
#include "ui_control.h"
#include "calculations.h"
#include "rally_types.h"
#include "rally_state.h"
#include "counter_poller.h"
#include "callbacks.h"
#include "config_file.h"
#include "webserver/qr_display.h"
#include "calculations.h"
#include "tone_generator.h"
#include <sstream>
#include <iostream>
#include <cstdlib>
#include <iomanip>
#include <ctime>
#include <string>
#include <vector>

// Apply CSS styling
static void applyCopilotCSS() {
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "window, .background { background-color: #000000; }"
        "label { color: #FFFFFF; font-weight: bold; }"
        "button { background-color: #333333; color: #FFFFFF; font-weight: bold; border: 2px solid #FFFFFF; }"
        "entry { background-color: #222222; color: #FFFFFF; font-weight: bold; }"
        ".title-label { font-size: 22px; }"
        ".info-label { font-size: 20px; }"
        ".clock-label { font-size: 30px; font-family: monospace; }"
        ".dist-heading { font-size: 28px; font-weight: bold; font-family: monospace; }"
        ".dist-value { font-size: 64px; font-weight: bold; font-family: monospace; }"
        ".dist-unit { font-size: 20px; font-weight: bold; font-family: monospace; }"
        ".time-label { font-size: 22px; font-family: monospace; color: #CCCCCC; }"
        ".alarm-label { font-size: 20px; min-width: 90px; }"
        ".alarm-button { font-size: 22px; }"
        ".reset-button { font-size: 36px; }"
        ".alarm-countdown { font-size: 28px; color: #FFFFFF; font-family: monospace; }"
        ".nav-button { font-size: 20px; }"
        ".segment-label { font-size: 18px; }"
        ".segment-row entry, .segment-row button, .segment-row checkbutton { font-size: 18px; }"
        ".new-segment-row label, .new-segment-row entry, .new-segment-row button, .new-segment-row checkbutton { font-size: 18px; }"
        ".segment-list-frame { border: 1px solid #444444; background-color: #111111; }"
        "scrollbar slider { min-width: 20px; min-height: 20px; }"
        "scrollbar.vertical slider { min-width: 20px; }"
        "scrollbar trough { min-width: 24px; }"
        "button.memory-populated { background-image: none; background-color: #FFFFFF; color: #000000; }"
        ".adjust-button { font-size: 28px; font-weight: bold; font-family: monospace; }"
        ".instruction-label { font-size: 16px; color: #CCCCCC; }"
        ".tone-mode-row label, .tone-mode-row checkbutton { font-size: 20px; }",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

// RB-SEG-04: Navigation and Timing modes share the same 3200 Hz sine pitch
// -- close to the peak of human hearing sensitivity (ear-canal resonance),
// well clear of age-related high-frequency hearing loss (presbycusis,
// usually starting above ~4 kHz) -- and are differentiated instead by
// repeat count (double vs. single beep) and duration (Timing's single beep
// is 2.5x as long as each Navigation beep).
static constexpr double BEEP_ASSIST_NAV_FREQ_HZ = 3200.0;
static constexpr ToneWaveform BEEP_ASSIST_NAV_WAVE = ToneWaveform::Sine;
static constexpr double BEEP_ASSIST_TIMING_FREQ_HZ = 3200.0;
static constexpr ToneWaveform BEEP_ASSIST_TIMING_WAVE = ToneWaveform::Sine;
// Own durations, independent of ToneGenerator::playBeep()'s 50ms default
// used elsewhere for the generic button-click feedback beep.
static constexpr int BEEP_ASSIST_NAV_DURATION_MS = 100;
static constexpr int BEEP_ASSIST_TIMING_DURATION_MS = 250;

// Plays and reports one beep -- shared by the independent navigation and
// timing checks in updateCopilotDisplay() so a tick where both are due
// fires both, back to back, rather than one silently eating the other's
// beep for the same waypoint.
static void fireBeepAssist(AppData* data, double waypoint_m, double travelled_m, bool navigation) {
    // Navigation mode plays its tone twice in quick succession ("bing
    // bong"), Timing mode plays it once.
    // No audio device in the sandbox (Docker/Xvfb) means playBeep() is
    // silently a no-op there -- this is the only visible confirmation
    // that the waypoint logic itself fired.
    std::cerr << "Beep Assist fired: waypoint " << waypoint_m
              << "m, travelled " << travelled_m << "m"
              << (navigation ? " (navigation, double beep)" : " (timing, single beep)")
              << std::endl;
    flashBeepWarning(data, navigation);
    if (data->toneGen) {
        double freq_hz = navigation ? BEEP_ASSIST_NAV_FREQ_HZ : BEEP_ASSIST_TIMING_FREQ_HZ;
        ToneWaveform wave = navigation ? BEEP_ASSIST_NAV_WAVE : BEEP_ASSIST_TIMING_WAVE;
        int duration_ms = navigation ? BEEP_ASSIST_NAV_DURATION_MS : BEEP_ASSIST_TIMING_DURATION_MS;
        data->toneGen->playBeep(freq_hz, wave, duration_ms);
        if (navigation) {
            // g_timeout_add, not a blocking sleep -- this runs on the GTK
            // main thread and must not stall the UI for 150ms.
            g_timeout_add(150, [](gpointer d) -> gboolean {
                static_cast<AppData*>(d)->toneGen->playBeep(BEEP_ASSIST_NAV_FREQ_HZ, BEEP_ASSIST_NAV_WAVE,
                                                             BEEP_ASSIST_NAV_DURATION_MS);
                return G_SOURCE_REMOVE;
            }, data);
        }
    }
}

void updateCopilotDisplay(AppData* data) {
    auto current_poll = data->poller->getMostRecent();
    auto current_time_ms = getRallyTime_ms(*data->state);
    
    // Rally clock
    std::string rally_time = formatTime(current_time_ms);
    gtk_label_set_text(data->copilotRallyClockLabel, rally_time.c_str());

    // Re-assert the window's fixed size every tick: GTK grows a window to
    // fit its widest-ever content but never shrinks it back down, so a
    // single transient spike in a distance value (e.g. a big Next-segment
    // reading, however briefly shown) would otherwise leave the co-pilot
    // window oversized -- overlapping the driver panel -- for the rest of
    // the session, even once the values shrink back to normal.
    if (!data->singleDisplayMode) {
        gtk_window_resize(GTK_WINDOW(data->copilotWindow), 1280, 400);
    }

    // Alarm check runs regardless of which screen is visible
    int64_t total_count_diff = calculateDistanceCounts(*data->state,
        current_poll.cntr1, current_poll.cntr2,
        data->state->total_start_cntr1, data->state->total_start_cntr2);

    // Beep Assist. Checked here rather than on the stage-setup screen so the
    // beeps keep coming while the operator is on any screen at all.
    if (data->state->beep_assist_enabled && !data->state->beep_waypoints_m.empty()) {
        double travelled_m = countsToMeters(total_count_diff, data->state->calibration);
        bool stage_active = data->state->segment_current_number >= 0;
        double elapsed_stage_s = (current_time_ms - data->state->total_start_time_ms) / 1000.0;

        // Navigation and timing are independent warnings, each checked
        // against its own cursor. A single shared cursor (the old design)
        // let whichever condition tripped first silently consume the OTHER
        // mode's beep for that same waypoint -- e.g. a navigation lead-in
        // (advance_m) tripping ahead of the scheduled time meant the timing
        // beep for that waypoint never fired at all, even though the two
        // are meant to be separate warnings that can both be due.
        long nav_due = dueBeepWaypoint(data->state->beep_waypoints_m, data->beepNextNavIndex,
                                       travelled_m,
                                       data->state->beep_navigation_mode, data->state->beep_advance_m,
                                       false, 0.0,
                                       stage_active, elapsed_stage_s, data->state->segments);
        if (nav_due >= 0) {
            fireBeepAssist(data, data->state->beep_waypoints_m[static_cast<size_t>(nav_due)],
                           travelled_m, /*navigation=*/true);
            data->beepNextNavIndex = static_cast<size_t>(nav_due) + 1;
        }

        long timing_due = dueBeepWaypoint(data->state->beep_waypoints_m, data->beepNextTimingIndex,
                                          travelled_m,
                                          false, 0.0,
                                          data->state->beep_timing_mode, data->state->beep_advance_s,
                                          stage_active, elapsed_stage_s, data->state->segments);
        if (timing_due >= 0) {
            fireBeepAssist(data, data->state->beep_waypoints_m[static_cast<size_t>(timing_due)],
                           travelled_m, /*navigation=*/false);
            data->beepNextTimingIndex = static_cast<size_t>(timing_due) + 1;
        }
    }

    // In single-display mode the countdown/clear widgets do not exist, but a
    // persisted alarm still fires its sound and auto-clears.
    if (data->state->alarm_distance_km > 0) {
        if (data->alarmClearBtn) gtk_widget_show(data->alarmClearBtn);
        int64_t remaining_counts = data->state->alarm_target_counts - total_count_diff;
        long remaining_m = countsToCentimeters(remaining_counts, data->state->calibration) / 100;
        
        if (remaining_m <= 0) {
            if (data->alarmSoundStartTime == 0) {
                data->alarmSoundStartTime = current_time_ms;
                system("aplay alarm.wav &");
            }
            if (data->alarmCountdownLabel) gtk_label_set_text(data->alarmCountdownLabel, "ALARM!");
            
            if (current_time_ms - data->alarmSoundStartTime > 5000) {
                data->state->alarm_distance_km = 0;
                data->state->alarm_target_counts = 0;
                data->alarmSoundStartTime = 0;
                if (data->alarmCountdownLabel) gtk_label_set_text(data->alarmCountdownLabel, "");
                if (data->alarmClearBtn) gtk_widget_hide(data->alarmClearBtn);
                ConfigFile::save(*data->state);
            }
        } else if (data->alarmCountdownLabel) {
            std::stringstream alarmSs;
            alarmSs << formatDistanceGrouped(remaining_m) << " to alarm";
            gtk_label_set_text(data->alarmCountdownLabel, alarmSs.str().c_str());
        }
    } else {
        if (data->alarmCountdownLabel) gtk_label_set_text(data->alarmCountdownLabel, "");
        if (data->alarmClearBtn) gtk_widget_hide(data->alarmClearBtn);
    }
    
    // Check which screen is visible
    GtkWidget* visible_child = gtk_stack_get_visible_child(data->copilotStack);
    
    // Update calibration screen if visible (continuous updates per design)
    if (visible_child == data->calibrationScreen) {
        updateCalibrationDisplay(data);
        return;
    }
    
    // Update date/time screen if visible (for live clock display)
    if (visible_child == data->dateTimeScreen) {
        updateDateTimeDisplay(data);
        return;
    }
    
    // Update auto-start screen if visible
    if (visible_child == data->autoStartScreen) {
        updateAutoStartDisplay(data);
        return;
    }
    
    // Only update TwinMaster if it's visible
    if (visible_child != data->twinMasterScreen) {
        return;
    }
    
    // Update Adj. driver Zero button label
    {
        double offset_s = data->state->ahead_behind_zero_offset_ms / 1000.0;
        char lbl[64];
        snprintf(lbl, sizeof(lbl), "driver Zero (%.2f)", offset_s);
        gtk_button_set_label(GTK_BUTTON(data->adjZeroBtn), lbl);
    }
    
    // Total distance
    long total_m = adjustedDistanceMeters(
        countsToCentimeters(total_count_diff, data->state->calibration),
        data->state->total_distance_adjust_cm);
    int64_t total_duration_ms = current_time_ms - data->state->total_start_time_ms;
    int64_t total_secs = total_duration_ms / 1000;
    
    std::stringstream ss;
    const char* total_unit = "m";
    std::string total_str = formatDistanceAutoUnit(total_m, &total_unit);
    gtk_label_set_text(data->totalDistLabel, total_str.c_str());
    gtk_label_set_text(data->totalUnitLabel, total_unit);

    gtk_label_set_text(data->totalTimeLabel, formatElapsedInterval(total_secs).c_str());
    
    // Trip distance
    int64_t trip_count_diff = calculateDistanceCounts(*data->state,
        current_poll.cntr1, current_poll.cntr2,
        data->state->trip_start_cntr1, data->state->trip_start_cntr2);
    long trip_m = adjustedDistanceMeters(
        countsToCentimeters(trip_count_diff, data->state->calibration),
        data->state->trip_distance_adjust_cm);
    int64_t trip_duration_ms = current_time_ms - data->state->trip_start_time_ms;
    int64_t trip_secs = trip_duration_ms / 1000;
    
    const char* trip_unit = "m";
    std::string trip_str = formatDistanceAutoUnit(trip_m, &trip_unit);
    gtk_label_set_text(data->tripDistLabel, trip_str.c_str());
    gtk_label_set_text(data->tripUnitLabel, trip_unit);

    gtk_label_set_text(data->tripTimeLabel, formatElapsedInterval(trip_secs).c_str());
    
    // Next segment info: distance remaining in current segment + speed of next segment
    if (data->state->segment_current_number >= 0 &&
        data->state->segment_current_number < static_cast<long>(data->state->segments.size())) {
        const Segment& cur_seg = data->state->segments[data->state->segment_current_number];
        int64_t seg_count_diff = calculateDistanceCounts(*data->state,
            current_poll.cntr1, current_poll.cntr2,
            data->state->segment_start_cntr1, data->state->segment_start_cntr2);
        int64_t remaining_counts = cur_seg.distance_counts - seg_count_diff;
        long remaining_cm = countsToCentimeters(remaining_counts, data->state->calibration);
        long remaining_m = (remaining_cm >= 0) ? (remaining_cm + 99) / 100 : -((-remaining_cm) / 100);
        long travelled_m = countsToCentimeters(seg_count_diff, data->state->calibration) / 100;
        
        const char* next_unit = "m";
        std::string next_str = formatDistanceAutoUnit(remaining_m, &next_unit);
        gtk_label_set_text(data->nextDistLabel, next_str.c_str());
        gtk_label_set_text(data->nextUnitLabel, next_unit);
        
        long next_seg_idx = data->state->segment_current_number + 1;
        bool has_next = next_seg_idx < static_cast<long>(data->state->segments.size());
        double next_kph = has_next ? data->state->segments[next_seg_idx].target_speed_kph : 0.0;
        double current_display_speed = cur_seg.target_speed_kph;
        double next_display_speed = next_kph;
        // Same conversion updateDriverDisplay() already applies to
        // targetSpeedLabel -- showing "40 mph" for a 40 km/h segment would be
        // wrong by a safety-relevant margin, not just a labelling nicety.
        if (data->state->units) {
            current_display_speed *= 0.621371;
            next_display_speed *= 0.621371;
        }
        gtk_label_set_text(data->nextSpeedLabel,
            segmentSpeedTransition(next_display_speed, has_next, data->state->units).c_str());

        gtk_button_set_label(GTK_BUTTON(data->nextPrevBtn),
            segmentRowHeading(current_display_speed, true).c_str());

        // next/prev button: active within 500m of segment end or start
        bool near_end = (remaining_m >= 0 && remaining_m <= 500) &&
                        (next_seg_idx < static_cast<long>(data->state->segments.size()));
        bool near_start = (travelled_m >= 0 && travelled_m <= 500) &&
                          (data->state->segment_current_number > 0);
        if (near_end) {
            gtk_button_set_label(GTK_BUTTON(data->nextPrevBtn), "next");
            gtk_widget_set_sensitive(data->nextPrevBtn, TRUE);
        } else if (near_start) {
            gtk_button_set_label(GTK_BUTTON(data->nextPrevBtn), "prev");
            gtk_widget_set_sensitive(data->nextPrevBtn, TRUE);
        } else {
            gtk_widget_set_sensitive(data->nextPrevBtn, FALSE);
        }
    } else if (data->state->segments.empty()) {
        gtk_label_set_text(data->nextDistLabel, "---.---");
        gtk_label_set_text(data->nextUnitLabel, "m");
        gtk_label_set_text(data->nextSpeedLabel, "---");
        gtk_button_set_label(GTK_BUTTON(data->nextPrevBtn), segmentRowHeading(0.0, false).c_str());
        gtk_widget_set_sensitive(data->nextPrevBtn, FALSE);
    } else {
        gtk_label_set_text(data->nextDistLabel, "---.---");
        gtk_label_set_text(data->nextUnitLabel, "m");
        gtk_label_set_text(data->nextSpeedLabel, segmentSpeedTransition(0.0, false, false).c_str());
        gtk_button_set_label(GTK_BUTTON(data->nextPrevBtn), segmentRowHeading(0.0, false).c_str());
        gtk_widget_set_sensitive(data->nextPrevBtn, FALSE);
    }
}

// Create TwinMaster screen - two-column layout for 1280x400
GtkWidget* createTwinMasterScreen(AppData* data) {
    GtkWidget* screen = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(screen), 5);
    
    // Main area: two columns (left 70%, right 30%)
    GtkWidget* mainArea = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(screen), mainArea, TRUE, TRUE, 0);
    
    // ── LEFT PANEL (70%): segment info, total, trip ──
    // Single-display mode: cede width to the embedded gauge panel
    GtkWidget* leftPanel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_size_request(leftPanel, data->singleDisplayMode ? -1 : 870, -1);
    gtk_box_pack_start(GTK_BOX(mainArea), leftPanel, TRUE, TRUE, 0);
    
    // Grid layout for Total/Trip/Next: columns = heading | value | unit | reset/arrow | time/speed
    GtkWidget* distGrid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(distGrid), 8);
    gtk_grid_set_row_spacing(GTK_GRID(distGrid), 2);
    gtk_box_pack_start(GTK_BOX(leftPanel), distGrid, FALSE, FALSE, 0);
    
    // Row 0: Total distance — heading "Total" is itself the reset button
    GtkWidget* totalHeadingBtn = gtk_button_new_with_label("Total");
    gtk_style_context_add_class(gtk_widget_get_style_context(totalHeadingBtn), "dist-heading");
    gtk_widget_set_valign(totalHeadingBtn, GTK_ALIGN_CENTER);
    g_signal_connect(totalHeadingBtn, "clicked", G_CALLBACK(on_total_reset), data);
    gtk_grid_attach(GTK_GRID(distGrid), totalHeadingBtn, 0, 0, 1, 1);
    
    data->totalDistLabel = GTK_LABEL(gtk_label_new("0"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->totalDistLabel)), "dist-value");
    gtk_label_set_xalign(data->totalDistLabel, 1.0);
    // Fixed at exactly 7 chars ("999,999", the widest 6-digit value) so the
    // rest of the row can never shift right, no matter how large the
    // underlying distance grows -- min and max both pinned, with ellipsis
    // at the front as the last resort if a value ever exceeds 6 digits.
    gtk_label_set_width_chars(data->totalDistLabel, 7);
    gtk_label_set_max_width_chars(data->totalDistLabel, 7);
    gtk_label_set_ellipsize(data->totalDistLabel, PANGO_ELLIPSIZE_START);
    gtk_grid_attach(GTK_GRID(distGrid), GTK_WIDGET(data->totalDistLabel), 1, 0, 1, 1);
    
    data->totalUnitLabel = GTK_LABEL(gtk_label_new("m"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->totalUnitLabel)), "dist-unit");
    gtk_widget_set_valign(GTK_WIDGET(data->totalUnitLabel), GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(distGrid), GTK_WIDGET(data->totalUnitLabel), 2, 0, 1, 1);
    
    // Col 3: Total time. Right-aligned in a fixed-width column rather than
    // space-padded, so the column holds whichever monospace font the platform
    // resolves -- Noto Sans Mono on the box, DejaVu in the sandbox.
    data->totalTimeLabel = GTK_LABEL(gtk_label_new("0:00"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->totalTimeLabel)), "time-label");
    gtk_label_set_xalign(data->totalTimeLabel, 1.0);
    gtk_widget_set_size_request(GTK_WIDGET(data->totalTimeLabel), 90, -1);
    gtk_widget_set_valign(GTK_WIDGET(data->totalTimeLabel), GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(GTK_WIDGET(data->totalTimeLabel), 10);
    if (!data->singleDisplayMode)  // hidden in single-display mode for a bigger gauge
        gtk_grid_attach(GTK_GRID(distGrid), GTK_WIDGET(data->totalTimeLabel), 3, 0, 1, 1);

    // Cols 4-6, Total row only: manual distance correction. Wheel slip or a
    // re-measured route leaves the odometer a known amount out; without this
    // the only remedy is a full reset, which throws the elapsed time away
    // too. -10/+10 adjust both Total and Trip (on_distance_adjust handles
    // that internally); "set" only ever resolves Total. There is no second
    // row of buttons on Trip -- these three cover both readouts.
    // Hidden in single-display mode, where the panel gives its width to the
    // embedded gauge and there is no room for them.
    if (!data->singleDisplayMode) {
        struct { const char* label; int delta_m; } controls[] = {
            { "-10", -10 }, { "set", 0 }, { "+10", 10 },
        };
        for (int c = 0; c < 3; c++) {
            GtkWidget* btn = gtk_button_new_with_label(controls[c].label);
            gtk_style_context_add_class(gtk_widget_get_style_context(btn), "adjust-button");
            gtk_widget_set_valign(btn, GTK_ALIGN_CENTER);
            if (c == 0) gtk_widget_set_margin_start(btn, 15);
            if (controls[c].delta_m == 0) {
                g_signal_connect(btn, "clicked", G_CALLBACK(on_distance_set), data);
            } else {
                g_object_set_data(G_OBJECT(btn), "delta_m", GINT_TO_POINTER(controls[c].delta_m));
                g_signal_connect(btn, "clicked", G_CALLBACK(on_distance_adjust), data);
            }
            gtk_grid_attach(GTK_GRID(distGrid), btn, 4 + c, 0, 1, 1);
        }
    }

    // Row 1: Trip distance — heading "Trip" is itself the reset button
    GtkWidget* tripHeadingBtn = gtk_button_new_with_label("Trip");
    gtk_style_context_add_class(gtk_widget_get_style_context(tripHeadingBtn), "dist-heading");
    gtk_widget_set_valign(tripHeadingBtn, GTK_ALIGN_CENTER);
    g_signal_connect(tripHeadingBtn, "clicked", G_CALLBACK(on_trip_reset), data);
    gtk_grid_attach(GTK_GRID(distGrid), tripHeadingBtn, 0, 1, 1, 1);
    
    data->tripDistLabel = GTK_LABEL(gtk_label_new("0"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->tripDistLabel)), "dist-value");
    gtk_label_set_xalign(data->tripDistLabel, 1.0);
    gtk_label_set_width_chars(data->tripDistLabel, 7);
    gtk_label_set_max_width_chars(data->tripDistLabel, 7);
    gtk_label_set_ellipsize(data->tripDistLabel, PANGO_ELLIPSIZE_START);
    gtk_grid_attach(GTK_GRID(distGrid), GTK_WIDGET(data->tripDistLabel), 1, 1, 1, 1);
    
    data->tripUnitLabel = GTK_LABEL(gtk_label_new("m"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->tripUnitLabel)), "dist-unit");
    gtk_widget_set_valign(GTK_WIDGET(data->tripUnitLabel), GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(distGrid), GTK_WIDGET(data->tripUnitLabel), 2, 1, 1, 1);
    
    // Col 3: Trip time. Right-aligned in a fixed-width column rather than
    // space-padded, so the column holds whichever monospace font the platform
    // resolves -- Noto Sans Mono on the box, DejaVu in the sandbox.
    data->tripTimeLabel = GTK_LABEL(gtk_label_new("0:00"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->tripTimeLabel)), "time-label");
    gtk_label_set_xalign(data->tripTimeLabel, 1.0);
    gtk_widget_set_size_request(GTK_WIDGET(data->tripTimeLabel), 90, -1);
    gtk_widget_set_valign(GTK_WIDGET(data->tripTimeLabel), GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(GTK_WIDGET(data->tripTimeLabel), 10);
    if (!data->singleDisplayMode)  // hidden in single-display mode for a bigger gauge
        gtk_grid_attach(GTK_GRID(distGrid), GTK_WIDGET(data->tripTimeLabel), 3, 1, 1, 1);
    
    // Row 2: Next segment — heading is the next/prev button
    data->nextPrevBtn = gtk_button_new_with_label("--->");
    gtk_style_context_add_class(gtk_widget_get_style_context(data->nextPrevBtn), "dist-heading");
    gtk_widget_set_valign(data->nextPrevBtn, GTK_ALIGN_CENTER);
    gtk_widget_set_sensitive(data->nextPrevBtn, FALSE);
    g_signal_connect(data->nextPrevBtn, "clicked", G_CALLBACK(on_next_prev_segment), data);
    gtk_grid_attach(GTK_GRID(distGrid), data->nextPrevBtn, 0, 2, 1, 1);
    
    data->nextDistLabel = GTK_LABEL(gtk_label_new("---.---"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->nextDistLabel)), "dist-value");
    gtk_label_set_xalign(data->nextDistLabel, 1.0);
    gtk_label_set_width_chars(data->nextDistLabel, 7);
    gtk_label_set_max_width_chars(data->nextDistLabel, 7);
    gtk_label_set_ellipsize(data->nextDistLabel, PANGO_ELLIPSIZE_START);
    gtk_grid_attach(GTK_GRID(distGrid), GTK_WIDGET(data->nextDistLabel), 1, 2, 1, 1);
    
    data->nextUnitLabel = GTK_LABEL(gtk_label_new("m"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->nextUnitLabel)), "dist-unit");
    gtk_widget_set_valign(GTK_WIDGET(data->nextUnitLabel), GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(distGrid), GTK_WIDGET(data->nextUnitLabel), 2, 2, 1, 1);
    
    // Col 3: Next segment speed
    data->nextSpeedLabel = GTK_LABEL(gtk_label_new("---"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->nextSpeedLabel)), "time-label");
    gtk_label_set_xalign(data->nextSpeedLabel, 0.0);
    gtk_widget_set_size_request(GTK_WIDGET(data->nextSpeedLabel), 90, -1);
    gtk_widget_set_valign(GTK_WIDGET(data->nextSpeedLabel), GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(GTK_WIDGET(data->nextSpeedLabel), 10);
    if (!data->singleDisplayMode)  // hidden in single-display mode for a bigger gauge
        gtk_grid_attach(GTK_GRID(distGrid), GTK_WIDGET(data->nextSpeedLabel), 3, 2, 1, 1);
    
    // ── RIGHT PANEL (30%) ──
    // Normal: clock, alarm buttons, countdown.
    // Single-display mode: embedded compact driver gauge instead (the rally
    // clock is drawn inside the gauge, alarms cannot be set).
    GtkWidget* rightPanel = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    // Single-display mode: wider panel (times/next-speed column is hidden) so
    // the gauge can grow until it is height-limited. The gauge area's hexpand
    // propagates to this panel, so it must fill its slot or it gets centred
    // with dead space either side.
    gtk_widget_set_size_request(rightPanel, data->singleDisplayMode ? 500 : 360, -1);
    gtk_box_pack_start(GTK_BOX(mainArea), rightPanel, FALSE, data->singleDisplayMode ? TRUE : FALSE, 0);

    if (data->singleDisplayMode) {
        data->copilotGaugeArea = gtk_drawing_area_new();
        gtk_widget_set_hexpand(data->copilotGaugeArea, TRUE);
        gtk_widget_set_vexpand(data->copilotGaugeArea, TRUE);
        g_signal_connect(data->copilotGaugeArea, "draw", G_CALLBACK(on_gauge_draw), data);
        gtk_box_pack_start(GTK_BOX(rightPanel), data->copilotGaugeArea, TRUE, TRUE, 0);

        data->alarmCountdownLabel = nullptr;
        data->alarmClearBtn = nullptr;
    } else {
    // Top row: rally clock
    GtkWidget* topRightRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(rightPanel), topRightRow, FALSE, FALSE, 0);

    gtk_label_set_width_chars(data->copilotRallyClockLabel, 8);
    gtk_label_set_xalign(data->copilotRallyClockLabel, 1.0);
    gtk_box_pack_end(GTK_BOX(topRightRow), GTK_WIDGET(data->copilotRallyClockLabel), FALSE, FALSE, 0);
    
    // Alarm buttons: label on its own row, then a flush-left 4x3 grid --
    // no per-row indent, so every column lines up squarely under the label
    // rather than being pushed right by a label-width spacer.
    GtkWidget* alarmBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(alarmBox, 10);
    gtk_widget_set_halign(alarmBox, GTK_ALIGN_END);
    gtk_box_pack_start(GTK_BOX(rightPanel), alarmBox, FALSE, FALSE, 0);

    // "KM", not "in": the buttons are kilometre distances, and
    // "Alarm in 2" reads as two minutes.
    GtkWidget* alarmLabel = gtk_label_new("Alarm KM");
    gtk_style_context_add_class(gtk_widget_get_style_context(alarmLabel), "alarm-label");
    gtk_label_set_xalign(GTK_LABEL(alarmLabel), 0.0);
    gtk_box_pack_start(GTK_BOX(alarmBox), alarmLabel, FALSE, FALSE, 0);

    // Alarm buttons: 4 rows of 3, 30% larger (62x47)
    auto addAlarmRow = [&](GtkWidget* parent, int from, int to) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
        gtk_box_pack_start(GTK_BOX(parent), row, FALSE, FALSE, 0);
        for (int km = from; km <= to; km++) {
            GtkWidget* btn = gtk_button_new_with_label(std::to_string(km).c_str());
            gtk_style_context_add_class(gtk_widget_get_style_context(btn), "alarm-button");
            gtk_widget_set_size_request(btn, 68, 47);
            g_object_set_data(G_OBJECT(btn), "km", GINT_TO_POINTER(km));
            g_signal_connect(btn, "clicked", G_CALLBACK(on_alarm_set), data);
            gtk_box_pack_start(GTK_BOX(row), btn, FALSE, FALSE, 2);
        }
    };

    addAlarmRow(alarmBox, 2, 4);
    addAlarmRow(alarmBox, 5, 7);
    addAlarmRow(alarmBox, 8, 10);
    addAlarmRow(alarmBox, 11, 13);
    
    // Alarm countdown + clear button, right-aligned under the alarm grid so
    // the countdown ends level with the buttons above it rather than
    // floating at the panel's left edge.
    GtkWidget* countdownRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_top(countdownRow, 8);
    gtk_widget_set_halign(countdownRow, GTK_ALIGN_END);
    gtk_box_pack_start(GTK_BOX(rightPanel), countdownRow, FALSE, FALSE, 0);

    data->alarmCountdownLabel = GTK_LABEL(gtk_label_new(""));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->alarmCountdownLabel)), "alarm-countdown");
    gtk_label_set_xalign(data->alarmCountdownLabel, 1.0);
    gtk_box_pack_start(GTK_BOX(countdownRow), GTK_WIDGET(data->alarmCountdownLabel), FALSE, FALSE, 0);

    data->alarmClearBtn = gtk_button_new_with_label("clear");
    gtk_style_context_add_class(gtk_widget_get_style_context(data->alarmClearBtn), "alarm-button");
    g_signal_connect(data->alarmClearBtn, "clicked", G_CALLBACK(on_alarm_clear), data);
    gtk_box_pack_start(GTK_BOX(countdownRow), data->alarmClearBtn, FALSE, FALSE, 5);
    gtk_widget_set_no_show_all(data->alarmClearBtn, TRUE);
    }
    
    // ── BOTTOM ROW: Navigation buttons (20% taller) ──
    GtkWidget* buttonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    gtk_widget_set_margin_start(buttonBox, 5);
    gtk_widget_set_margin_end(buttonBox, 5);
    gtk_box_pack_end(GTK_BOX(screen), buttonBox, FALSE, FALSE, 0);
    
    GtkWidget* stageGoBtn = gtk_button_new_with_label("stage go");
    GtkWidget* segmentsBtn = gtk_button_new_with_label("segments");
    data->adjZeroBtn = gtk_button_new_with_label("driver Zero (0.00)");
    GtkWidget* calBtn = gtk_button_new_with_label("calibration");
    GtkWidget* datetimeBtn = gtk_button_new_with_label("date/time");

    GtkWidget* navBtns[] = {stageGoBtn, segmentsBtn, data->adjZeroBtn, calBtn, datetimeBtn};
    for (auto* btn : navBtns) {
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "nav-button");
        gtk_widget_set_size_request(btn, -1, 43);
        gtk_box_pack_start(GTK_BOX(buttonBox), btn, TRUE, TRUE, 0);
    }

    g_signal_connect(stageGoBtn, "clicked", G_CALLBACK(on_stage_go), data);
    g_signal_connect(segmentsBtn, "clicked", G_CALLBACK(on_show_segments), data);
    g_signal_connect(data->adjZeroBtn, "clicked", G_CALLBACK(on_adj_driver_zero), data);
    g_signal_connect(calBtn, "clicked", G_CALLBACK(on_show_calibration), data);
    g_signal_connect(datetimeBtn, "clicked", G_CALLBACK(on_show_datetime), data);
    
    return screen;
}

// Create Stage Setup screen - horizontal layout for 1280x400
GtkWidget* createStageSetupScreen(AppData* data) {
    GtkWidget* screen = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_set_border_width(GTK_CONTAINER(screen), 5);
    
    // Title
    GtkWidget* titleLabel = gtk_label_new("STAGE SETUP");
    gtk_style_context_add_class(gtk_widget_get_style_context(titleLabel), "title-label");
    gtk_box_pack_start(GTK_BOX(screen), titleLabel, FALSE, FALSE, 0);
    
    // Main horizontal container: left side for segments, right side for keypad
    data->stageSetupMainBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(screen), data->stageSetupMainBox, TRUE, TRUE, 0);
    
    // Left side: the segment table. Cropped to its content width rather than
    // expanding to fill the panel -- the New: row below is the widest thing
    // in it, so this column ends right after "add". The space that frees up
    // goes to Beep Assist (RB-SEG-02), not to this list.
    GtkWidget* leftBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(data->stageSetupMainBox), leftBox, FALSE, FALSE, 0);

    // Header row, on the same fixed column widths as the data rows below.
    GtkWidget* headerGrid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(headerGrid), SEGMENT_COL_SPACING);
    gtk_box_pack_start(GTK_BOX(leftBox), headerGrid, FALSE, FALSE, 0);

    struct { const char* text; int width; } headers[] = {
        { "Speed (KPH)",  SEGMENT_COL_SPEED },
        { "Distance (m)", SEGMENT_COL_DISTANCE },
        { "Auto",         SEGMENT_COL_AUTO },
        { "Time",         SEGMENT_COL_TIME },
        { "",             SEGMENT_COL_DELETE },
    };
    for (int i = 0; i < 5; i++) {
        GtkWidget* lbl = gtk_label_new(headers[i].text);
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "segment-label");
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
        gtk_widget_set_size_request(lbl, headers[i].width, -1);
        gtk_grid_attach(GTK_GRID(headerGrid), lbl, i, 0, 1, 1);
    }

    // Scrollable list, framed so its extent is visible against the black
    // background -- an unframed list reads as loose text on the panel.
    GtkWidget* listFrame = gtk_frame_new(NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(listFrame), "segment-list-frame");
    gtk_box_pack_start(GTK_BOX(leftBox), listFrame, TRUE, TRUE, 0);

    GtkWidget* scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled), 150);
    gtk_container_add(GTK_CONTAINER(listFrame), scrolled);

    data->segmentListBox = GTK_LIST_BOX(gtk_list_box_new());
    gtk_list_box_set_selection_mode(data->segmentListBox, GTK_SELECTION_NONE);
    gtk_container_add(GTK_CONTAINER(scrolled), GTK_WIDGET(data->segmentListBox));

    // Memory columns (Set and Recall) - vertical layout between segments and keypad
    GtkWidget* memBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(data->stageSetupMainBox), memBox, FALSE, FALSE, 10);
    
    // Header row for memory columns
    GtkWidget* memHeaderRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(memBox), memHeaderRow, FALSE, FALSE, 0);
    GtkWidget* setHeader = gtk_label_new("Set");
    GtkWidget* recallHeader = gtk_label_new("Recall");
    gtk_widget_set_size_request(setHeader, 66, -1);
    gtk_widget_set_size_request(recallHeader, 66, -1);
    gtk_box_pack_start(GTK_BOX(memHeaderRow), setHeader, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(memHeaderRow), recallHeader, FALSE, FALSE, 0);
    
    // Buttons [1]-[5] in two vertical columns
    for (int i = 1; i <= 5; i++) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
        gtk_box_pack_start(GTK_BOX(memBox), row, FALSE, FALSE, 0);
        
        GtkWidget* setBtn = gtk_button_new_with_label(std::to_string(i).c_str());
        gtk_widget_set_size_request(setBtn, 66, 43);
        g_signal_connect(setBtn, "clicked", G_CALLBACK(on_memory_set), data);
        g_object_set_data(G_OBJECT(setBtn), "slot", GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(row), setBtn, FALSE, FALSE, 0);
        
        GtkWidget* recallBtn = gtk_button_new_with_label(std::to_string(i).c_str());
        gtk_widget_set_size_request(recallBtn, 66, 43);
        g_signal_connect(recallBtn, "clicked", G_CALLBACK(on_memory_recall), data);
        g_object_set_data(G_OBJECT(recallBtn), "slot", GINT_TO_POINTER(i));
        gtk_box_pack_start(GTK_BOX(row), recallBtn, FALSE, FALSE, 0);
        data->memoryRecallBtns[i - 1] = recallBtn;
    }
    
    // Style populated recall buttons
    for (int i = 0; i < 5; i++) {
        if (!data->state->memory_slots[i].empty()) {
            gtk_style_context_add_class(
                gtk_widget_get_style_context(data->memoryRecallBtns[i]), "memory-populated");
        }
    }
    
    GtkWidget* clearMemBtn = gtk_button_new_with_label("clear memory");
    g_signal_connect(clearMemBtn, "clicked", G_CALLBACK(on_memory_clear), data);
    gtk_box_pack_start(GTK_BOX(memBox), clearMemBtn, FALSE, FALSE, 5);

    // Beep Assist: waypoints the operator wants a warning at, so their eyes
    // can stay on the roadbook rather than the odometer. Its own column,
    // between the memory buttons and the keypad.
    // Width is not an explicit size_request -- it's set by beepTitle's own
    // natural size ("Beep Assist KM"), and every row below it is
    // built to fit within that same budget rather than forcing the column
    // wider. A size_request minimum was tried here through several rounds
    // (260/200/150px) and had zero effect: the leadIns rows below always
    // needed ~370px regardless of what floor was set on the column itself.
    GtkWidget* beepCol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_pack_start(GTK_BOX(data->stageSetupMainBox), beepCol, FALSE, FALSE, 0);

    // Wraps rather than setting the column's width -- "Off/On ... Advance
    // by" is the narrower, authoritative row; the title would otherwise be
    // the widest thing in the column and push it out past "by".
    GtkWidget* beepTitle = gtk_label_new("Beep Assist KM");
    gtk_style_context_add_class(gtk_widget_get_style_context(beepTitle), "segment-label");
    gtk_label_set_xalign(GTK_LABEL(beepTitle), 0.0);
    gtk_label_set_line_wrap(GTK_LABEL(beepTitle), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(beepTitle), 14);
    gtk_box_pack_start(GTK_BOX(beepCol), beepTitle, FALSE, FALSE, 0);

    GtkWidget* beepFrame = gtk_frame_new(NULL);
    gtk_style_context_add_class(gtk_widget_get_style_context(beepFrame), "segment-list-frame");
    gtk_box_pack_start(GTK_BOX(beepCol), beepFrame, TRUE, TRUE, 0);

    GtkWidget* beepView = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(beepView), GTK_WRAP_WORD_CHAR);
    gtk_style_context_add_class(gtk_widget_get_style_context(beepView), "segment-label");
    g_signal_connect(beepView, "focus-in-event", G_CALLBACK(on_textview_focus), data);
    gtk_container_add(GTK_CONTAINER(beepFrame), beepView);

    data->beepWaypointBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(beepView));
    gtk_text_buffer_set_text(data->beepWaypointBuffer,
        formatBeepWaypointsKm(data->state->beep_waypoints_m).c_str(), -1);

    // Re-derive the runtime cursor from the distance already travelled so
    // restarting the app mid-stage does not replay every waypoint already
    // passed. Mirrors the recompute in on_beep_waypoints_changed, which only
    // fires on operator edits and never runs at startup since the buffer is
    // populated before that signal is connected (deliberately, to avoid a
    // spurious save-on-load).
    auto initPoll = data->poller->getMostRecent();
    int64_t initCounts = calculateDistanceCounts(*data->state, initPoll.cntr1, initPoll.cntr2,
                                                 data->state->total_start_cntr1,
                                                 data->state->total_start_cntr2);
    double initTravelled_m = countsToMeters(initCounts, data->state->calibration);
    data->beepNextNavIndex = beepCursorFor(data->state->beep_waypoints_m, initTravelled_m);
    data->beepNextTimingIndex = beepCursorFor(data->state->beep_waypoints_m, initTravelled_m);

    g_signal_connect(data->beepWaypointBuffer, "changed",
                     G_CALLBACK(on_beep_waypoints_changed), data);

    // Forces every row below to the same width as the widest one (whichever
    // that turns out to be) so the entry fields genuinely align to the same
    // right edge as "Advance by" -- hexpand does not work for this: it
    // propagates a widget's expand request up through its parent boxes,
    // but beepCol itself is packed with expand=FALSE into stageSetupMainBox,
    // which blocks that propagation, so a bare hexpand on a row never
    // actually stretched it past its own natural content width.
    GtkSizeGroup* beepRowWidth = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

    // Off/On
    GtkWidget* activeRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    gtk_size_group_add_widget(beepRowWidth, activeRow);
    gtk_box_pack_start(GTK_BOX(beepCol), activeRow, FALSE, FALSE, 0);

    GtkWidget* beepOnLabel = gtk_label_new("Off/On");
    gtk_style_context_add_class(gtk_widget_get_style_context(beepOnLabel), "segment-label");
    gtk_box_pack_start(GTK_BOX(activeRow), beepOnLabel, FALSE, FALSE, 0);

    GtkWidget* beepSwitch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(beepSwitch), data->state->beep_assist_enabled);
    gtk_widget_set_valign(beepSwitch, GTK_ALIGN_CENTER);
    g_signal_connect(beepSwitch, "state-set", G_CALLBACK(on_beep_enable_toggled), data);
    gtk_box_pack_start(GTK_BOX(activeRow), beepSwitch, FALSE, FALSE, 0);

    // "Mode" heads the checkbox column shared by the two rows below, the
    // same way "Advance by" heads their entry column -- so each row itself
    // only needs the bare mode name. Both headings share their own row,
    // separate from "Off/On" above them: Mode left-aligned, Advance by
    // right-aligned.
    GtkWidget* headingRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_size_group_add_widget(beepRowWidth, headingRow);
    gtk_box_pack_start(GTK_BOX(beepCol), headingRow, FALSE, FALSE, 0);

    GtkWidget* modeHeading = gtk_label_new("Mode");
    gtk_style_context_add_class(gtk_widget_get_style_context(modeHeading), "segment-label");
    gtk_label_set_xalign(GTK_LABEL(modeHeading), 0.0);
    gtk_box_pack_start(GTK_BOX(headingRow), modeHeading, FALSE, FALSE, 0);

    GtkWidget* advanceHeading = gtk_label_new("Advance by");
    gtk_style_context_add_class(gtk_widget_get_style_context(advanceHeading), "segment-label");
    gtk_box_pack_end(GTK_BOX(headingRow), advanceHeading, FALSE, FALSE, 0);

    // Two independent lead-in rows: navigation mode is a fixed distance,
    // timing mode is a fixed number of seconds off the scheduled (roadbook)
    // arrival time. Both may be on; either firing is enough.
    //
    // Root cause of the column's width overflow, confirmed by measuring
    // gtk_widget_get_preferred_width() on each row's individual children
    // (querying the GtkSizeGroup-member rows themselves is invalid -- a
    // SizeGroup member always reports the group's synced width, which is
    // what sent the first two rounds of measurement down the wrong path):
    // the Metres/Seconds GtkEntry's natural width was 168px. The 90px in
    // gtk_widget_set_size_request(entry, 90, 30) is only a *minimum* --
    // with no gtk_entry_set_width_chars()/max_width_chars() call, GTK fell
    // back to its own default natural-width heuristic for a bare entry,
    // nearly double the intended 90px.
    struct { const char* mode_label; const char* placeholder; int mode; } leadIns[] = {
        { "Navigation", "Metres",  0 },
        { "Timing",     "Seconds", 1 },
    };
    for (const auto& li : leadIns) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_size_group_add_widget(beepRowWidth, row);
        gtk_box_pack_start(GTK_BOX(beepCol), row, FALSE, FALSE, 0);

        GtkWidget* modeLabel = gtk_label_new(li.mode_label);
        gtk_style_context_add_class(gtk_widget_get_style_context(modeLabel), "segment-label");
        gtk_label_set_xalign(GTK_LABEL(modeLabel), 0.0);
        gtk_box_pack_start(GTK_BOX(row), modeLabel, FALSE, FALSE, 0);

        GtkWidget* check = gtk_check_button_new();
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check),
            li.mode == 1 ? data->state->beep_timing_mode : data->state->beep_navigation_mode);
        g_object_set_data(G_OBJECT(check), "mode", GINT_TO_POINTER(li.mode));
        g_signal_connect(check, "toggled", G_CALLBACK(on_beep_mode_toggled), data);
        gtk_box_pack_start(GTK_BOX(row), check, FALSE, FALSE, 0);

        // Single-line height (30px), matching the checkbox/label row rather
        // than the taller 40px used elsewhere for touch targets. Width
        // explicitly capped at 6 chars ("999999") -- both the floor and
        // the ceiling GTK needs to stop defaulting to ~168px.
        GtkEntry* entry = GTK_ENTRY(gtk_entry_new());
        gtk_entry_set_placeholder_text(entry, li.placeholder);
        gtk_entry_set_width_chars(entry, 6);
        gtk_entry_set_max_width_chars(entry, 6);
        gtk_widget_set_size_request(GTK_WIDGET(entry), -1, 30);
        double stored = (li.mode == 1) ? data->state->beep_advance_s : data->state->beep_advance_m;
        if (stored > 0.0) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.0f", stored);
            gtk_entry_set_text(entry, buf);
        }
        g_object_set_data(G_OBJECT(entry), "mode", GINT_TO_POINTER(li.mode));
        g_signal_connect(entry, "focus-in-event", G_CALLBACK(on_entry_focus), data);
        g_signal_connect(entry, "changed", G_CALLBACK(on_beep_advance_changed), data);
        gtk_box_pack_end(GTK_BOX(row), GTK_WIDGET(entry), FALSE, FALSE, 0);

        if (li.mode == 1) data->beepAdvanceSecondsEntry = entry;
        else              data->beepAdvanceMetresEntry = entry;
    }

    // Add new segment row (30% larger fonts and buttons)
    GtkWidget* addBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(addBox), "new-segment-row");
    gtk_box_pack_start(GTK_BOX(leftBox), addBox, FALSE, FALSE, 5);
    
    GtkWidget* newLabel = gtk_label_new("New:");
    data->targetSpeedEntry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(data->targetSpeedEntry, "KPH");
    gtk_widget_set_size_request(GTK_WIDGET(data->targetSpeedEntry), 70, 40);
    g_signal_connect(data->targetSpeedEntry, "focus-in-event", G_CALLBACK(on_entry_focus), data);

    data->distanceEntry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(data->distanceEntry, "metres (;  separated)");
    gtk_widget_set_size_request(GTK_WIDGET(data->distanceEntry), 180, 40);
    g_signal_connect(data->distanceEntry, "focus-in-event", G_CALLBACK(on_entry_focus), data);
    
    data->autoNextCheck = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Auto"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->autoNextCheck), TRUE);
    
    GtkWidget* addBtn = gtk_button_new_with_label("add");
    gtk_widget_set_size_request(addBtn, 60, 40);
    g_signal_connect(addBtn, "clicked", G_CALLBACK(on_add_segment), data);
    
    gtk_box_pack_start(GTK_BOX(addBox), newLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(addBox), GTK_WIDGET(data->targetSpeedEntry), FALSE, FALSE, 3);
    gtk_box_pack_start(GTK_BOX(addBox), GTK_WIDGET(data->distanceEntry), FALSE, FALSE, 3);
    gtk_box_pack_start(GTK_BOX(addBox), GTK_WIDGET(data->autoNextCheck), FALSE, FALSE, 3);
    gtk_box_pack_start(GTK_BOX(addBox), addBtn, FALSE, FALSE, 3);

    // Right side: keypad, with "back" on its own row beneath it, in the same
    // column. It used to sit in the New: row next to "add", where a mis-tap
    // during data entry dropped the operator out of the screen mid-edit. (A
    // fully separate column for "back" was tried and dropped -- it only
    // squeezed Beep Assist for no real benefit.)
    GtkWidget* rightCol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_box_pack_end(GTK_BOX(data->stageSetupMainBox), rightCol, FALSE, FALSE, 10);

    data->numericKeypad = createNumericKeypad(data);
    gtk_box_pack_start(GTK_BOX(rightCol), data->numericKeypad, FALSE, FALSE, 0);

    // Tone mode: master on/off, plus which algorithm (Type 1 = existing
    // speed/arrow-based tone, the default; Type 2 = alternative
    // time-error-only tone). Placed under the keypad -- an option chosen
    // once before driving, not something adjusted mid-stage.
    GtkWidget* toneModeRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(toneModeRow), "tone-mode-row");
    GtkWidget* toneModeLabel = gtk_label_new("tone off/on");
    GtkWidget* toneModeSwitch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(toneModeSwitch), data->state->tone_enabled);
    gtk_widget_set_valign(toneModeSwitch, GTK_ALIGN_CENTER);
    g_signal_connect(toneModeSwitch, "state-set", G_CALLBACK(on_tone_enabled_toggle), data);
    gtk_box_pack_start(GTK_BOX(toneModeRow), toneModeLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toneModeRow), toneModeSwitch, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rightCol), toneModeRow, FALSE, FALSE, 0);

    GtkWidget* toneTypeRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(toneTypeRow), "tone-mode-row");
    GtkWidget* toneTypeLabel = gtk_label_new("Type");
    GtkWidget* toneType1Check = gtk_check_button_new_with_label("1");
    GtkWidget* toneType2Check = gtk_check_button_new_with_label("2");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toneType1Check), !data->state->simple_tone_mode);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toneType2Check), data->state->simple_tone_mode);
    g_object_set_data(G_OBJECT(toneType1Check), "tone_type", GINT_TO_POINTER(1));
    g_object_set_data(G_OBJECT(toneType2Check), "tone_type", GINT_TO_POINTER(2));
    g_object_set_data(G_OBJECT(toneType1Check), "other_type_check", toneType2Check);
    g_object_set_data(G_OBJECT(toneType2Check), "other_type_check", toneType1Check);
    g_signal_connect(toneType1Check, "toggled", G_CALLBACK(on_tone_type_toggled), data);
    g_signal_connect(toneType2Check, "toggled", G_CALLBACK(on_tone_type_toggled), data);
    gtk_box_pack_start(GTK_BOX(toneTypeRow), toneTypeLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toneTypeRow), toneType1Check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(toneTypeRow), toneType2Check, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rightCol), toneTypeRow, FALSE, FALSE, 0);

    GtkWidget* backBtn = gtk_button_new_with_label("back");
    gtk_widget_set_size_request(backBtn, -1, 40);
    gtk_widget_set_valign(backBtn, GTK_ALIGN_END);
    g_signal_connect(backBtn, "clicked", G_CALLBACK(on_show_twinmaster), data);
    gtk_box_pack_end(GTK_BOX(rightCol), backBtn, FALSE, FALSE, 0);

    data->activeEntry = NULL;
    
    return screen;
}

// Create Calibration screen - horizontal layout for 1280x400
GtkWidget* createCalibrationScreen(AppData* data) {
    GtkWidget* screen = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_style_context_add_class(gtk_widget_get_style_context(screen), "calibration-screen");
    gtk_container_set_border_width(GTK_CONTAINER(screen), 5);
    
    // Title, with the rally clock on the same row -- this screen otherwise
    // has no clock at all, unlike every other screen.
    GtkWidget* titleRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(screen), titleRow, FALSE, FALSE, 0);

    GtkWidget* titleLabel = gtk_label_new("CALIBRATION");
    gtk_style_context_add_class(gtk_widget_get_style_context(titleLabel), "title-label");
    gtk_widget_set_hexpand(titleLabel, TRUE);
    gtk_box_pack_start(GTK_BOX(titleRow), titleLabel, TRUE, TRUE, 0);

    data->calibrationClockLabel = GTK_LABEL(gtk_label_new("00:00:00"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->calibrationClockLabel)), "clock-label");
    gtk_label_set_width_chars(data->calibrationClockLabel, 8);
    gtk_label_set_xalign(data->calibrationClockLabel, 1.0);
    gtk_box_pack_end(GTK_BOX(titleRow), GTK_WIDGET(data->calibrationClockLabel), FALSE, FALSE, 0);
    
    // Main horizontal container: left side for info, right side for keypad
    data->calibrationMainBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(screen), data->calibrationMainBox, TRUE, TRUE, 0);
    
    // Left side: info and input
    GtkWidget* leftBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(data->calibrationMainBox), leftBox, TRUE, TRUE, 0);
    
    // Row 1: Distance with counter breakdown
    // Format: "Total distance: xxx,xxx m  (counts calculated: CNTR_A  1: CNTR_1  2: CNTR_2)"
    data->totalDistCalLabel = GTK_LABEL(gtk_label_new("Total distance: 0 m  (counts calculated: 0   1: 0   2: 0)"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->totalDistCalLabel)), "clock-label");
    gtk_widget_set_halign(GTK_WIDGET(data->totalDistCalLabel), GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(leftBox), GTK_WIDGET(data->totalDistCalLabel), FALSE, FALSE, 0);

    // Row 2: Input field
    GtkWidget* inputRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(leftBox), inputRow, FALSE, FALSE, 0);

    GtkWidget* inputLabel = gtk_label_new("Actual distance covered:");
    gtk_style_context_add_class(gtk_widget_get_style_context(inputLabel), "clock-label");
    data->rallyDistEntry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(data->rallyDistEntry, "500-100000");
    gtk_widget_set_size_request(GTK_WIDGET(data->rallyDistEntry), 150, -1);
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->rallyDistEntry)), "clock-label");
    g_signal_connect(data->rallyDistEntry, "focus-in-event", G_CALLBACK(on_entry_focus), data);
    GtkWidget* unitLabel = gtk_label_new("metres");
    gtk_style_context_add_class(gtk_widget_get_style_context(unitLabel), "clock-label");

    gtk_box_pack_start(GTK_BOX(inputRow), inputLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(inputRow), GTK_WIDGET(data->rallyDistEntry), FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(inputRow), unitLabel, FALSE, FALSE, 0);

    // Row 3: Sensor mode selection
    GtkWidget* sensorRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(leftBox), sensorRow, FALSE, FALSE, 0);

    data->sensorModeLabel = GTK_LABEL(gtk_label_new(""));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->sensorModeLabel)), "clock-label");
    gtk_widget_set_halign(GTK_WIDGET(data->sensorModeLabel), GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(sensorRow), GTK_WIDGET(data->sensorModeLabel), FALSE, FALSE, 0);
    updateSensorModeLabel(data);
    
    GtkWidget* sensor1Btn = gtk_button_new_with_label("Set sensor 1");
    g_signal_connect(sensor1Btn, "clicked", G_CALLBACK(on_set_sensor_1), data);
    gtk_box_pack_start(GTK_BOX(sensorRow), sensor1Btn, FALSE, FALSE, 20);
    
    GtkWidget* sensorBothBtn = gtk_button_new_with_label("Set both sensors and avg.");
    g_signal_connect(sensorBothBtn, "clicked", G_CALLBACK(on_set_sensor_both), data);
    gtk_box_pack_start(GTK_BOX(sensorRow), sensorBothBtn, FALSE, FALSE, 5);

    // The saved value and the reset control share one line: both describe
    // the same figure, so a separate row for each would repeat "pulses/KM"
    // for no reason. The live figure is highlighted the same yellow the
    // sensor line uses -- like sensor selection, it is a number every other
    // reading on the screen is derived from.
    GtkWidget* currentCalRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_top(currentCalRow, 6);
    gtk_box_pack_start(GTK_BOX(leftBox), currentCalRow, FALSE, FALSE, 0);

    data->calibrationCurrentLabel = GTK_LABEL(gtk_label_new(NULL));
    gtk_label_set_markup(GTK_LABEL(data->calibrationCurrentLabel),
        "Current <span foreground=\"#FFDD00\">Calibration 0 pulses/KM</span>. Reset to");
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->calibrationCurrentLabel)), "clock-label");
    gtk_widget_set_halign(GTK_WIDGET(data->calibrationCurrentLabel), GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(currentCalRow), GTK_WIDGET(data->calibrationCurrentLabel), FALSE, FALSE, 0);

    data->resetPulsesEntry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_width_chars(data->resetPulsesEntry, 6);
    gtk_entry_set_max_width_chars(data->resetPulsesEntry, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->resetPulsesEntry)), "clock-label");
    g_signal_connect(data->resetPulsesEntry, "focus-in-event", G_CALLBACK(on_entry_focus), data);
    gtk_box_pack_start(GTK_BOX(currentCalRow), GTK_WIDGET(data->resetPulsesEntry), FALSE, FALSE, 0);

    GtkWidget* resetPulsesBtn = gtk_button_new_with_label("Reset");
    g_signal_connect(resetPulsesBtn, "clicked", G_CALLBACK(on_reset_calibration_pulses), data);
    gtk_box_pack_start(GTK_BOX(currentCalRow), resetPulsesBtn, FALSE, FALSE, 0);

    // Calibration is a rare, four-step procedure done at the roadside under
    // time pressure. Spelling the steps out costs six rows and saves a manual.
    const char* instructions[] = {
        "INSTRUCTIONS:",
        "1. Press Start/Zero to reset counter to zero or skip to step 2 to use existing counters.",
        "2. Enter actual distance covered.",
        "3. Press Sensor button to select counter.",
        "4. Save Calibration",
    };
    for (int i = 0; i < 5; i++) {
        GtkWidget* lbl = gtk_label_new(instructions[i]);
        gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "instruction-label");
        gtk_widget_set_halign(lbl, GTK_ALIGN_START);
        if (i == 0) gtk_widget_set_margin_top(lbl, 22);
        gtk_box_pack_start(GTK_BOX(leftBox), lbl, FALSE, FALSE, 0);
    }

    // Right side: numeric keypad. Nudged down from the top of its column so
    // it doesn't sit flush against the title row now that the row is taller
    // (title + clock) than a plain title alone.
    data->calibrationKeypad = createNumericKeypad(data);
    gtk_widget_set_margin_top(data->calibrationKeypad, 12);
    gtk_box_pack_end(GTK_BOX(data->calibrationMainBox), data->calibrationKeypad, FALSE, FALSE, 10);
    
    // Bottom: navigation buttons
    GtkWidget* buttonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_box_pack_end(GTK_BOX(screen), buttonBox, FALSE, FALSE, 5);
    
    // "start" did not say that it zeroes the counters, which is the one
    // irreversible thing on this screen.
    GtkWidget* startBtn = gtk_button_new_with_label("Start/Zero");
    GtkWidget* saveBtn = gtk_button_new_with_label("Save Calibration");
    GtkWidget* backBtn = gtk_button_new_with_label("Back");
    
    g_signal_connect(startBtn, "clicked", G_CALLBACK(on_calibration_start), data);
    g_signal_connect(saveBtn, "clicked", G_CALLBACK(on_save_calibration), data);
    g_signal_connect(backBtn, "clicked", G_CALLBACK(on_show_twinmaster), data);
    
    gtk_box_pack_start(GTK_BOX(buttonBox), startBtn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(buttonBox), saveBtn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(buttonBox), backBtn, TRUE, TRUE, 0);
    
    // Hide totalCountCalLabel - we're using totalDistCalLabel for everything
    data->totalCountCalLabel = GTK_LABEL(gtk_label_new(""));
    
    return screen;
}

// Create Date/Time Setup screen - horizontal layout for 1280x400
GtkWidget* createDateTimeScreen(AppData* data) {
    GtkWidget* screen = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(screen), "datetime-screen");
    gtk_container_set_border_width(GTK_CONTAINER(screen), 5);
    
    // Title row with exit button
    GtkWidget* titleRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(screen), titleRow, FALSE, FALSE, 0);
    
    GtkWidget* titleLabel = gtk_label_new("DATE/TIME SETUP");
    gtk_style_context_add_class(gtk_widget_get_style_context(titleLabel), "title-label");
    gtk_box_pack_start(GTK_BOX(titleRow), titleLabel, TRUE, TRUE, 0);
    
    GtkWidget* exitBtn = gtk_button_new_with_label("exit app");
    g_signal_connect(exitBtn, "clicked", G_CALLBACK(on_exit_app), data);
    gtk_box_pack_end(GTK_BOX(titleRow), exitBtn, FALSE, FALSE, 0);
    
    // Main horizontal container: left side for content, right side for keypad
    GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(screen), mainBox, TRUE, TRUE, 0);
    
    // Left side: clocks, input, buttons
    GtkWidget* leftBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(mainBox), leftBox, FALSE, FALSE, 0);
    
    // Clocks and the rally-time input share a grid so the date/time input boxes
    // line up under the date and time columns of the clock readouts:
    //   col 0 = row label, col 1 = date, col 2 = time.
    GtkWidget* clockGrid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(clockGrid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(clockGrid), 16);
    gtk_box_pack_start(GTK_BOX(leftBox), clockGrid, FALSE, FALSE, 0);

    // Row 0: System clock (date | time)
    GtkWidget* sysLabel = gtk_label_new("System Clock:");
    gtk_style_context_add_class(gtk_widget_get_style_context(sysLabel), "clock-label");
    gtk_widget_set_halign(sysLabel, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(clockGrid), sysLabel, 0, 0, 1, 1);
    data->systemClockLabel = GTK_LABEL(gtk_label_new(""));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->systemClockLabel)), "clock-label");
    gtk_label_set_xalign(data->systemClockLabel, 0.0);
    gtk_grid_attach(GTK_GRID(clockGrid), GTK_WIDGET(data->systemClockLabel), 1, 0, 1, 1);
    data->systemTimeLabel = GTK_LABEL(gtk_label_new(""));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->systemTimeLabel)), "clock-label");
    gtk_label_set_xalign(data->systemTimeLabel, 0.0);
    gtk_grid_attach(GTK_GRID(clockGrid), GTK_WIDGET(data->systemTimeLabel), 2, 0, 1, 1);

    // Row 1: Rally clock (date | time)
    GtkWidget* rallyLabel = gtk_label_new("Rally  Clock:");
    gtk_style_context_add_class(gtk_widget_get_style_context(rallyLabel), "clock-label");
    gtk_widget_set_halign(rallyLabel, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(clockGrid), rallyLabel, 0, 1, 1, 1);
    data->rallyClockLabel = GTK_LABEL(gtk_label_new(""));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->rallyClockLabel)), "clock-label");
    gtk_label_set_xalign(data->rallyClockLabel, 0.0);
    gtk_grid_attach(GTK_GRID(clockGrid), GTK_WIDGET(data->rallyClockLabel), 1, 1, 1, 1);
    data->rallyTimeLabel = GTK_LABEL(gtk_label_new(""));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->rallyTimeLabel)), "clock-label");
    gtk_label_set_xalign(data->rallyTimeLabel, 0.0);
    gtk_grid_attach(GTK_GRID(clockGrid), GTK_WIDGET(data->rallyTimeLabel), 2, 1, 1, 1);

    // Row 2: Set rally clock — date box under the date column, time box under time
    GtkWidget* setLabel = gtk_label_new("Set Rally Clk:");
    gtk_style_context_add_class(gtk_widget_get_style_context(setLabel), "clock-label");
    gtk_widget_set_halign(setLabel, GTK_ALIGN_START);
    gtk_widget_set_margin_top(setLabel, 6);
    gtk_grid_attach(GTK_GRID(clockGrid), setLabel, 0, 2, 1, 1);

    data->dateEntry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(data->dateEntry, "yyyy/mm/dd");
    gtk_entry_set_width_chars(data->dateEntry, 10);
    gtk_widget_set_margin_top(GTK_WIDGET(data->dateEntry), 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->dateEntry)), "clock-label");
    g_signal_connect(data->dateEntry, "focus-in-event", G_CALLBACK(on_entry_focus), data);
    gtk_grid_attach(GTK_GRID(clockGrid), GTK_WIDGET(data->dateEntry), 1, 2, 1, 1);

    data->timeEntry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(data->timeEntry, "hh:mm:ss");
    gtk_entry_set_width_chars(data->timeEntry, 8);
    gtk_widget_set_margin_top(GTK_WIDGET(data->timeEntry), 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->timeEntry)), "clock-label");
    g_signal_connect(data->timeEntry, "focus-in-event", G_CALLBACK(on_entry_focus), data);
    gtk_grid_attach(GTK_GRID(clockGrid), GTK_WIDGET(data->timeEntry), 2, 2, 1, 1);
    
    // Options section
    GtkWidget* optionsLabel = gtk_label_new("Options:");
    gtk_style_context_add_class(gtk_widget_get_style_context(optionsLabel), "clock-label");
    gtk_widget_set_halign(optionsLabel, GTK_ALIGN_START);
    gtk_widget_set_margin_top(optionsLabel, 10);
    gtk_box_pack_start(GTK_BOX(leftBox), optionsLabel, FALSE, FALSE, 0);
    
    GtkWidget* forceSingleRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    GtkWidget* forceSingleLabel = gtk_label_new("force single display mode");
    gtk_style_context_add_class(gtk_widget_get_style_context(forceSingleLabel), "clock-label");
    GtkWidget* forceSingleSwitch = gtk_switch_new();
    gtk_switch_set_active(GTK_SWITCH(forceSingleSwitch), data->state->force_single_display);
    gtk_widget_set_valign(forceSingleSwitch, GTK_ALIGN_CENTER);
    g_signal_connect(forceSingleSwitch, "state-set",
                     G_CALLBACK(on_force_single_display_toggle), data);
    gtk_box_pack_start(GTK_BOX(forceSingleRow), forceSingleLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(forceSingleRow), forceSingleSwitch, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(leftBox), forceSingleRow, FALSE, FALSE, 0);

    // Speed units (KPH/MPH) toggle — moved here from the driver display. The button
    // caption shows the current unit; pressing it flips KPH <-> MPH (on_unit_toggle).
    GtkWidget* unitsRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    GtkWidget* unitsRowLabel = gtk_label_new("speed units");
    gtk_style_context_add_class(gtk_widget_get_style_context(unitsRowLabel), "clock-label");
    data->unitToggleBtn = GTK_BUTTON(gtk_button_new_with_label(data->state->units ? "MPH" : "KPH"));
    gtk_widget_set_valign(GTK_WIDGET(data->unitToggleBtn), GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(unitsRow), unitsRowLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(unitsRow), GTK_WIDGET(data->unitToggleBtn), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(leftBox), unitsRow, FALSE, FALSE, 0);

    // Middle column: phone web access (URL + QR) placed in the open space to the
    // right of the clock rows (top-aligned, URL level with the System Clock row),
    // between the narrowed left column and the keypad, so it neither overflows the
    // left column nor pushes the keypad off-screen.
    GtkWidget* webBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_valign(webBox, GTK_ALIGN_START);
    gtk_widget_set_halign(webBox, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(mainBox), webBox, TRUE, TRUE, 0);

    data->webUrlLabel = GTK_LABEL(gtk_label_new(""));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->webUrlLabel)), "clock-label");
    gtk_label_set_selectable(data->webUrlLabel, TRUE);
    gtk_box_pack_start(GTK_BOX(webBox), GTK_WIDGET(data->webUrlLabel), FALSE, FALSE, 0);

    data->webQrArea = gtk_drawing_area_new();
    gtk_widget_set_size_request(data->webQrArea, 132, 132);
    gtk_widget_set_halign(data->webQrArea, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(data->webQrArea, 6);
    g_signal_connect(data->webQrArea, "draw", G_CALLBACK(on_qr_draw), data);
    gtk_box_pack_start(GTK_BOX(webBox), data->webQrArea, FALSE, FALSE, 0);

    // Right side: datetime keypad
    data->datetimeKeypad = createDateTimeKeypad(data);
    gtk_box_pack_end(GTK_BOX(mainBox), data->datetimeKeypad, FALSE, FALSE, 10);
    
    // Bottom: navigation buttons
    GtkWidget* buttonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_box_pack_end(GTK_BOX(screen), buttonBox, FALSE, FALSE, 5);
    
    GtkWidget* saveBtn = gtk_button_new_with_label("set and save");
    GtkWidget* backBtn = gtk_button_new_with_label("back");
    
    g_signal_connect(saveBtn, "clicked", G_CALLBACK(on_save_datetime), data);
    g_signal_connect(backBtn, "clicked", G_CALLBACK(on_show_twinmaster), data);
    
    gtk_box_pack_start(GTK_BOX(buttonBox), saveBtn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(buttonBox), backBtn, TRUE, TRUE, 0);
    
    return screen;
}

// Create Auto Start Setup screen
GtkWidget* createAutoStartScreen(AppData* data) {
    GtkWidget* screen = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_style_context_add_class(gtk_widget_get_style_context(screen), "datetime-screen");
    gtk_container_set_border_width(GTK_CONTAINER(screen), 5);
    
    GtkWidget* titleLabel = gtk_label_new("AUTO START SETUP");
    gtk_style_context_add_class(gtk_widget_get_style_context(titleLabel), "title-label");
    gtk_box_pack_start(GTK_BOX(screen), titleLabel, FALSE, FALSE, 0);
    
    GtkWidget* mainBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(screen), mainBox, TRUE, TRUE, 0);
    
    GtkWidget* leftBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(mainBox), leftBox, TRUE, TRUE, 0);
    
    // Rally clock row
    GtkWidget* rallyRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* rallyLabel = gtk_label_new("Rally  Clock:");
    gtk_style_context_add_class(gtk_widget_get_style_context(rallyLabel), "clock-label");
    data->autoStartRallyClockLabel = GTK_LABEL(gtk_label_new(""));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->autoStartRallyClockLabel)), "clock-label");
    gtk_box_pack_start(GTK_BOX(rallyRow), rallyLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(rallyRow), GTK_WIDGET(data->autoStartRallyClockLabel), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(leftBox), rallyRow, FALSE, FALSE, 0);
    
    // Auto start current value row
    GtkWidget* autoRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* autoLabel = gtk_label_new("Auto Start:");
    gtk_style_context_add_class(gtk_widget_get_style_context(autoLabel), "clock-label");
    data->autoStartTimeLabel = GTK_LABEL(gtk_label_new(""));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->autoStartTimeLabel)), "clock-label");
    gtk_box_pack_start(GTK_BOX(autoRow), autoLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(autoRow), GTK_WIDGET(data->autoStartTimeLabel), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(leftBox), autoRow, FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(leftBox), gtk_label_new(""), FALSE, FALSE, 0);
    
    // Input row
    GtkWidget* inputRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    GtkWidget* setLabel = gtk_label_new("Set Auto Start time within 3 Hours:");
    gtk_style_context_add_class(gtk_widget_get_style_context(setLabel), "clock-label");
    data->autoStartTimeEntry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(data->autoStartTimeEntry, "hh:mm:ss");
    gtk_widget_set_size_request(GTK_WIDGET(data->autoStartTimeEntry), 200, -1);
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->autoStartTimeEntry)), "clock-label");
    g_signal_connect(data->autoStartTimeEntry, "focus-in-event", G_CALLBACK(on_entry_focus), data);
    gtk_box_pack_start(GTK_BOX(inputRow), setLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(inputRow), GTK_WIDGET(data->autoStartTimeEntry), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(leftBox), inputRow, FALSE, FALSE, 0);
    
    // Right side: datetime keypad (same layout)
    data->autoStartKeypad = createDateTimeKeypad(data);
    gtk_box_pack_end(GTK_BOX(mainBox), data->autoStartKeypad, FALSE, FALSE, 10);
    
    // Bottom: navigation buttons
    GtkWidget* buttonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_box_pack_end(GTK_BOX(screen), buttonBox, FALSE, FALSE, 5);
    
    GtkWidget* clearBtn = gtk_button_new_with_label("clear");
    GtkWidget* setBtn = gtk_button_new_with_label("set");
    GtkWidget* backBtn = gtk_button_new_with_label("back");
    
    g_signal_connect(clearBtn, "clicked", G_CALLBACK(on_autostart_clear), data);
    g_signal_connect(setBtn, "clicked", G_CALLBACK(on_autostart_set), data);
    g_signal_connect(backBtn, "clicked", G_CALLBACK(on_show_twinmaster), data);
    
    gtk_box_pack_start(GTK_BOX(buttonBox), clearBtn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(buttonBox), setBtn, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(buttonBox), backBtn, TRUE, TRUE, 0);
    
    return screen;
}

GtkWidget* createCopilotWindow(AppData* data) {
    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Co-Pilot Display");
    gtk_window_set_default_size(GTK_WINDOW(window), 1280, 400);
    
    applyCopilotCSS();
    
    // Create rally clock label first (used by TwinMaster screen)
    data->copilotRallyClockLabel = GTK_LABEL(gtk_label_new("00:00:00"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->copilotRallyClockLabel)), "clock-label");
    
    // Create stack for screens
    data->copilotStack = GTK_STACK(gtk_stack_new());
    // Size each screen to its own content, not the tallest child. The Date/Time
    // screen (with the phone-access QR code) is taller than the 400px window;
    // with the default vhomogeneous=TRUE it would inflate every screen's minimum
    // height and push the TwinMaster nav bar off the bottom of the display.
    gtk_stack_set_vhomogeneous(data->copilotStack, FALSE);
    gtk_stack_set_hhomogeneous(data->copilotStack, FALSE);
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(data->copilotStack));
    
    // Create all screens
    data->twinMasterScreen = createTwinMasterScreen(data);
    data->stageSetupScreen = createStageSetupScreen(data);
    data->calibrationScreen = createCalibrationScreen(data);
    data->dateTimeScreen = createDateTimeScreen(data);
    data->autoStartScreen = createAutoStartScreen(data);
    
    // Add screens to stack
    gtk_stack_add_named(data->copilotStack, data->twinMasterScreen, "twinmaster");
    gtk_stack_add_named(data->copilotStack, data->stageSetupScreen, "stagesetup");
    gtk_stack_add_named(data->copilotStack, data->calibrationScreen, "calibration");
    gtk_stack_add_named(data->copilotStack, data->dateTimeScreen, "datetime");
    gtk_stack_add_named(data->copilotStack, data->autoStartScreen, "autostart");
    
    // Show TwinMaster by default
    gtk_stack_set_visible_child_name(data->copilotStack, "twinmaster");
    
    return window;
}
