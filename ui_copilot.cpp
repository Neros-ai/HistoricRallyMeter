#include "ui_copilot.h"
#include "ui_driver.h"
#include "calculations.h"
#include "rally_types.h"
#include "rally_state.h"
#include "counter_poller.h"
#include "callbacks.h"
#include "config_file.h"
#include "webserver/qr_display.h"
#include "calculations.h"
#include <sstream>
#include <cstdlib>
#include <iomanip>
#include <ctime>
#include <string>

static std::string formatDistance(long meters, int width = 7) {
    bool negative = meters < 0;
    std::string num = std::to_string(negative ? -meters : meters);
    std::string result;
    int count = 0;
    for (int i = static_cast<int>(num.size()) - 1; i >= 0; i--) {
        if (count > 0 && count % 3 == 0) result = ',' + result;
        result = num[i] + result;
        count++;
    }
    if (negative) result = '-' + result;
    while (static_cast<int>(result.size()) < width)
        result = ' ' + result;
    return result;
}

// Format elapsed time as space-padded minutes (3+ chars) + ":" + zero-padded
// seconds. When minutes need more than 4 chars, switch to hours:minutes;
// when hours need more than 4 chars, display "toolong".
static std::string formatElapsed(int64_t total_secs) {
    if (total_secs < 0) total_secs = 0;
    int64_t minutes = total_secs / 60;
    char buf[24];
    if (minutes <= 9999) {
        snprintf(buf, sizeof(buf), " %3lld:%02lld",
                 static_cast<long long>(minutes),
                 static_cast<long long>(total_secs % 60));
    } else {
        int64_t hours = minutes / 60;
        if (hours <= 9999) {
            snprintf(buf, sizeof(buf), " %3lld:%02lld",
                     static_cast<long long>(hours),
                     static_cast<long long>(minutes % 60));
        } else {
            snprintf(buf, sizeof(buf), "toolong");
        }
    }
    return buf;
}

// Format a distance, switching to km above 999,999 m so the value stays
// within the fixed display width. Sets *unit_out to "m" or "km".
static std::string formatDistanceAuto(long meters, const char** unit_out, int width = 7) {
    if (meters > 999999 || meters < -999999) {
        *unit_out = "km";
        return formatDistance(meters / 1000, width);
    }
    *unit_out = "m";
    return formatDistance(meters, width);
}

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
        ".dist-heading { font-size: 48px; font-weight: bold; font-family: monospace; }"
        ".dist-value { font-size: 88px; font-weight: bold; font-family: monospace; }"
        ".dist-unit { font-size: 48px; font-weight: bold; font-family: monospace; }"
        ".time-label { font-size: 36px; font-family: monospace; color: #CCCCCC; }"
        ".alarm-label { font-size: 20px; }"
        ".alarm-button { font-size: 22px; }"
        ".reset-button { font-size: 36px; }"
        ".alarm-countdown { font-size: 28px; color: #FFFFFF; font-family: monospace; }"
        ".nav-button { font-size: 20px; }"
        ".segment-label { font-size: 18px; }"
        ".segment-row entry, .segment-row button, .segment-row checkbutton { font-size: 18px; }"
        ".new-segment-row label, .new-segment-row entry, .new-segment-row button, .new-segment-row checkbutton { font-size: 18px; }"
        "scrollbar slider { min-width: 20px; min-height: 20px; }"
        "scrollbar.vertical slider { min-width: 20px; }"
        "scrollbar trough { min-width: 24px; }"
        "button.memory-populated { background-image: none; background-color: #FFFFFF; color: #000000; }",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

void updateCopilotDisplay(AppData* data) {
    auto current_poll = data->poller->getMostRecent();
    auto current_time_ms = getRallyTime_ms(*data->state);
    
    // Rally clock
    std::string rally_time = formatTime(current_time_ms);
    gtk_label_set_text(data->copilotRallyClockLabel, rally_time.c_str());
    
    // Alarm check runs regardless of which screen is visible
    int64_t total_count_diff = calculateDistanceCounts(*data->state,
        current_poll.cntr1, current_poll.cntr2,
        data->state->total_start_cntr1, data->state->total_start_cntr2);
    
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
            alarmSs << formatDistance(remaining_m, 6) << " to alarm";
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
        snprintf(lbl, sizeof(lbl), "Adj. driver Zero (%.2fs)", offset_s);
        gtk_button_set_label(GTK_BUTTON(data->adjZeroBtn), lbl);
    }
    
    // Total distance
    long total_m = countsToCentimeters(total_count_diff, data->state->calibration) / 100;
    int64_t total_duration_ms = current_time_ms - data->state->total_start_time_ms;
    int64_t total_secs = total_duration_ms / 1000;
    
    std::stringstream ss;
    const char* total_unit = "m";
    std::string total_str = formatDistanceAuto(total_m, &total_unit);
    gtk_label_set_text(data->totalDistLabel, total_str.c_str());
    gtk_label_set_text(data->totalUnitLabel, total_unit);
    
    gtk_label_set_text(data->totalTimeLabel, formatElapsed(total_secs).c_str());
    
    // Trip distance
    int64_t trip_count_diff = calculateDistanceCounts(*data->state,
        current_poll.cntr1, current_poll.cntr2,
        data->state->trip_start_cntr1, data->state->trip_start_cntr2);
    long trip_m = countsToCentimeters(trip_count_diff, data->state->calibration) / 100;
    int64_t trip_duration_ms = current_time_ms - data->state->trip_start_time_ms;
    int64_t trip_secs = trip_duration_ms / 1000;
    
    const char* trip_unit = "m";
    std::string trip_str = formatDistanceAuto(trip_m, &trip_unit);
    gtk_label_set_text(data->tripDistLabel, trip_str.c_str());
    gtk_label_set_text(data->tripUnitLabel, trip_unit);
    
    gtk_label_set_text(data->tripTimeLabel, formatElapsed(trip_secs).c_str());
    
    // Next segment info: distance remaining in current segment + speed of next segment
    if (data->state->segment_current_number >= 0 &&
        data->state->segment_current_number < static_cast<long>(data->state->stage_segments.size())) {
        const Segment& cur_seg = data->state->stage_segments[data->state->segment_current_number];
        int64_t seg_count_diff = calculateDistanceCounts(*data->state,
            current_poll.cntr1, current_poll.cntr2,
            data->state->segment_start_cntr1, data->state->segment_start_cntr2);
        int64_t remaining_counts = cur_seg.distance_counts - seg_count_diff;
        long remaining_cm = countsToCentimeters(remaining_counts, data->state->calibration);
        long remaining_m = (remaining_cm >= 0) ? (remaining_cm + 99) / 100 : -((-remaining_cm) / 100);
        long travelled_m = countsToCentimeters(seg_count_diff, data->state->calibration) / 100;
        
        const char* next_unit = "m";
        std::string next_str = formatDistanceAuto(remaining_m, &next_unit);
        gtk_label_set_text(data->nextDistLabel, next_str.c_str());
        gtk_label_set_text(data->nextUnitLabel, next_unit);
        
        long next_seg_idx = data->state->segment_current_number + 1;
        if (next_seg_idx < static_cast<long>(data->state->stage_segments.size())) {
            const Segment& next_seg = data->state->stage_segments[next_seg_idx];
            ss.str("");
            ss << std::fixed << std::setprecision(0) << next_seg.target_speed_kph << " kph";
            gtk_label_set_text(data->nextSpeedLabel, ss.str().c_str());
        } else {
            gtk_label_set_text(data->nextSpeedLabel, "END");
        }
        
        // next/prev button: active within 500m of segment end or start
        bool near_end = (remaining_m >= 0 && remaining_m <= 500) &&
                        (next_seg_idx < static_cast<long>(data->state->stage_segments.size()));
        bool near_start = (travelled_m >= 0 && travelled_m <= 500) &&
                          (data->state->segment_current_number > 0);
        if (near_end) {
            gtk_button_set_label(GTK_BUTTON(data->nextPrevBtn), "next");
            gtk_widget_set_sensitive(data->nextPrevBtn, TRUE);
        } else if (near_start) {
            gtk_button_set_label(GTK_BUTTON(data->nextPrevBtn), "prev");
            gtk_widget_set_sensitive(data->nextPrevBtn, TRUE);
        } else {
            gtk_button_set_label(GTK_BUTTON(data->nextPrevBtn), "--->");
            gtk_widget_set_sensitive(data->nextPrevBtn, FALSE);
        }
    } else if (data->state->stage_segments.empty()) {
        gtk_label_set_text(data->nextDistLabel, "---.---");
        gtk_label_set_text(data->nextUnitLabel, "m");
        gtk_label_set_text(data->nextSpeedLabel, "---");
        gtk_button_set_label(GTK_BUTTON(data->nextPrevBtn), "--->");
        gtk_widget_set_sensitive(data->nextPrevBtn, FALSE);
    } else {
        gtk_label_set_text(data->nextDistLabel, "---.---");
        gtk_label_set_text(data->nextUnitLabel, "m");
        gtk_label_set_text(data->nextSpeedLabel, "END");
        gtk_button_set_label(GTK_BUTTON(data->nextPrevBtn), "--->");
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
    gtk_label_set_width_chars(data->totalDistLabel, 7);
    gtk_grid_attach(GTK_GRID(distGrid), GTK_WIDGET(data->totalDistLabel), 1, 0, 1, 1);
    
    data->totalUnitLabel = GTK_LABEL(gtk_label_new("m"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->totalUnitLabel)), "dist-unit");
    gtk_widget_set_valign(GTK_WIDGET(data->totalUnitLabel), GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(distGrid), GTK_WIDGET(data->totalUnitLabel), 2, 0, 1, 1);
    
    // Col 3: Total time
    data->totalTimeLabel = GTK_LABEL(gtk_label_new("   0:00"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->totalTimeLabel)), "time-label");
    gtk_label_set_xalign(data->totalTimeLabel, 0.0);
    gtk_widget_set_valign(GTK_WIDGET(data->totalTimeLabel), GTK_ALIGN_CENTER);
    gtk_widget_set_margin_start(GTK_WIDGET(data->totalTimeLabel), 10);
    if (!data->singleDisplayMode)  // hidden in single-display mode for a bigger gauge
        gtk_grid_attach(GTK_GRID(distGrid), GTK_WIDGET(data->totalTimeLabel), 3, 0, 1, 1);
    
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
    gtk_grid_attach(GTK_GRID(distGrid), GTK_WIDGET(data->tripDistLabel), 1, 1, 1, 1);
    
    data->tripUnitLabel = GTK_LABEL(gtk_label_new("m"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->tripUnitLabel)), "dist-unit");
    gtk_widget_set_valign(GTK_WIDGET(data->tripUnitLabel), GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(distGrid), GTK_WIDGET(data->tripUnitLabel), 2, 1, 1, 1);
    
    // Col 3: Trip time
    data->tripTimeLabel = GTK_LABEL(gtk_label_new("   0:00"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->tripTimeLabel)), "time-label");
    gtk_label_set_xalign(data->tripTimeLabel, 0.0);
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
    gtk_grid_attach(GTK_GRID(distGrid), GTK_WIDGET(data->nextDistLabel), 1, 2, 1, 1);
    
    data->nextUnitLabel = GTK_LABEL(gtk_label_new("m"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->nextUnitLabel)), "dist-unit");
    gtk_widget_set_valign(GTK_WIDGET(data->nextUnitLabel), GTK_ALIGN_END);
    gtk_grid_attach(GTK_GRID(distGrid), GTK_WIDGET(data->nextUnitLabel), 2, 2, 1, 1);
    
    // Col 3: Next segment speed
    data->nextSpeedLabel = GTK_LABEL(gtk_label_new("---"));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->nextSpeedLabel)), "time-label");
    gtk_label_set_xalign(data->nextSpeedLabel, 0.0);
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
    
    // Alarm buttons: 3 rows of 4
    GtkWidget* alarmBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_set_margin_top(alarmBox, 10);
    gtk_box_pack_start(GTK_BOX(rightPanel), alarmBox, FALSE, FALSE, 0);
    
    // Alarm buttons: 4 rows of 3, 30% larger (62x47)
    auto addAlarmRow = [&](GtkWidget* parent, int from, int to, bool hasLabel) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 3);
        gtk_box_pack_start(GTK_BOX(parent), row, FALSE, FALSE, 0);
        if (hasLabel) {
            GtkWidget* lbl = gtk_label_new("Alarm in");
            gtk_style_context_add_class(gtk_widget_get_style_context(lbl), "alarm-label");
            gtk_box_pack_start(GTK_BOX(row), lbl, FALSE, FALSE, 3);
        } else {
            GtkWidget* spacer = gtk_label_new("");
            gtk_style_context_add_class(gtk_widget_get_style_context(spacer), "alarm-label");
            gtk_widget_set_size_request(spacer, 70, -1);
            gtk_box_pack_start(GTK_BOX(row), spacer, FALSE, FALSE, 3);
        }
        for (int km = from; km <= to; km++) {
            GtkWidget* btn = gtk_button_new_with_label(std::to_string(km).c_str());
            gtk_style_context_add_class(gtk_widget_get_style_context(btn), "alarm-button");
            gtk_widget_set_size_request(btn, 62, 47);
            g_object_set_data(G_OBJECT(btn), "km", GINT_TO_POINTER(km));
            g_signal_connect(btn, "clicked", G_CALLBACK(on_alarm_set), data);
            gtk_box_pack_start(GTK_BOX(row), btn, FALSE, FALSE, 2);
        }
    };
    
    addAlarmRow(alarmBox, 2, 4, true);
    addAlarmRow(alarmBox, 5, 7, false);
    addAlarmRow(alarmBox, 8, 10, false);
    addAlarmRow(alarmBox, 11, 13, false);
    
    // Alarm countdown + clear button
    GtkWidget* countdownRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_widget_set_margin_top(countdownRow, 8);
    gtk_box_pack_start(GTK_BOX(rightPanel), countdownRow, FALSE, FALSE, 0);
    
    data->alarmCountdownLabel = GTK_LABEL(gtk_label_new(""));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(data->alarmCountdownLabel)), "alarm-countdown");
    gtk_box_pack_start(GTK_BOX(countdownRow), GTK_WIDGET(data->alarmCountdownLabel), FALSE, FALSE, 0);
    
    data->alarmClearBtn = gtk_button_new_with_label("clear");
    gtk_style_context_add_class(gtk_widget_get_style_context(data->alarmClearBtn), "alarm-button");
    g_signal_connect(data->alarmClearBtn, "clicked", G_CALLBACK(on_alarm_clear), data);
    gtk_box_pack_start(GTK_BOX(countdownRow), data->alarmClearBtn, FALSE, FALSE, 5);
    gtk_widget_set_no_show_all(data->alarmClearBtn, TRUE);
    }
    
    // ── BOTTOM ROW: Navigation buttons (20% taller) ──
    GtkWidget* buttonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 15);
    gtk_box_pack_end(GTK_BOX(screen), buttonBox, FALSE, FALSE, 0);
    
    GtkWidget* stageGoBtn = gtk_button_new_with_label("stage go");
    GtkWidget* segmentsBtn = gtk_button_new_with_label("segments");
    data->adjZeroBtn = gtk_button_new_with_label("Adj. driver Zero (0.00s)");
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
    
    // Left side: segments list
    GtkWidget* leftBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_widget_set_size_request(leftBox, 500, -1);
    gtk_box_pack_start(GTK_BOX(data->stageSetupMainBox), leftBox, FALSE, FALSE, 0);
    
    // Header row
    GtkWidget* headerBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(leftBox), headerBox, FALSE, FALSE, 0);
    
    GtkWidget* header1 = gtk_label_new("Speed (KPH)");
    GtkWidget* header2 = gtk_label_new("Distance (m)");
    GtkWidget* header3 = gtk_label_new("Auto");
    GtkWidget* header4 = gtk_label_new("Time");
    
    gtk_style_context_add_class(gtk_widget_get_style_context(header1), "segment-label");
    gtk_style_context_add_class(gtk_widget_get_style_context(header2), "segment-label");
    gtk_style_context_add_class(gtk_widget_get_style_context(header3), "segment-label");
    gtk_style_context_add_class(gtk_widget_get_style_context(header4), "segment-label");
    
    gtk_box_pack_start(GTK_BOX(headerBox), header1, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(headerBox), header2, FALSE, FALSE, 80);
    gtk_box_pack_start(GTK_BOX(headerBox), header3, FALSE, FALSE, 5);
    gtk_box_pack_start(GTK_BOX(headerBox), header4, FALSE, FALSE, 5);
    
    // Scrollable list for segments
    GtkWidget* scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), 
                                   GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scrolled), 150);
    gtk_box_pack_start(GTK_BOX(leftBox), scrolled, TRUE, TRUE, 0);
    
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
    
    // Add new segment row (30% larger fonts and buttons)
    GtkWidget* addBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(addBox), "new-segment-row");
    gtk_box_pack_start(GTK_BOX(leftBox), addBox, FALSE, FALSE, 5);
    
    GtkWidget* newLabel = gtk_label_new("New:");
    data->targetSpeedEntry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(data->targetSpeedEntry, "KPH");
    gtk_widget_set_size_request(GTK_WIDGET(data->targetSpeedEntry), 100, 40);
    g_signal_connect(data->targetSpeedEntry, "focus-in-event", G_CALLBACK(on_entry_focus), data);
    
    data->distanceEntry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(data->distanceEntry, "meters (;  separated)");
    gtk_widget_set_size_request(GTK_WIDGET(data->distanceEntry), 300, 40);
    g_signal_connect(data->distanceEntry, "focus-in-event", G_CALLBACK(on_entry_focus), data);
    
    data->autoNextCheck = GTK_CHECK_BUTTON(gtk_check_button_new_with_label("Auto"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->autoNextCheck), TRUE);
    
    GtkWidget* addBtn = gtk_button_new_with_label("add");
    gtk_widget_set_size_request(addBtn, 60, 40);
    g_signal_connect(addBtn, "clicked", G_CALLBACK(on_add_segment), data);
    
    GtkWidget* backBtn = gtk_button_new_with_label("back");
    gtk_widget_set_size_request(backBtn, 60, 40);
    g_signal_connect(backBtn, "clicked", G_CALLBACK(on_show_twinmaster), data);
    
    gtk_box_pack_start(GTK_BOX(addBox), newLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(addBox), GTK_WIDGET(data->targetSpeedEntry), FALSE, FALSE, 3);
    gtk_box_pack_start(GTK_BOX(addBox), GTK_WIDGET(data->distanceEntry), FALSE, FALSE, 3);
    gtk_box_pack_start(GTK_BOX(addBox), GTK_WIDGET(data->autoNextCheck), FALSE, FALSE, 3);
    gtk_box_pack_start(GTK_BOX(addBox), addBtn, FALSE, FALSE, 3);
    gtk_box_pack_start(GTK_BOX(addBox), backBtn, FALSE, FALSE, 20);
    
    // Right side: numeric keypad (initially hidden, shown when entry focused)
    data->numericKeypad = createNumericKeypad(data);
    gtk_box_pack_end(GTK_BOX(data->stageSetupMainBox), data->numericKeypad, FALSE, FALSE, 10);
    
    data->activeEntry = NULL;
    
    return screen;
}

// Create Calibration screen - horizontal layout for 1280x400
GtkWidget* createCalibrationScreen(AppData* data) {
    GtkWidget* screen = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_style_context_add_class(gtk_widget_get_style_context(screen), "calibration-screen");
    gtk_container_set_border_width(GTK_CONTAINER(screen), 5);
    
    // Title
    GtkWidget* titleLabel = gtk_label_new("CALIBRATION");
    gtk_style_context_add_class(gtk_widget_get_style_context(titleLabel), "title-label");
    gtk_box_pack_start(GTK_BOX(screen), titleLabel, FALSE, FALSE, 0);
    
    // Main horizontal container: left side for info, right side for keypad
    data->calibrationMainBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(screen), data->calibrationMainBox, TRUE, TRUE, 0);
    
    // Left side: info and input
    GtkWidget* leftBox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_box_pack_start(GTK_BOX(data->calibrationMainBox), leftBox, TRUE, TRUE, 0);
    
    // Row 1: Distance with counter breakdown
    // Format: "Total distance: xxx,xxx m  (counts calculated: CNTR_A  1: CNTR_1  2: CNTR_2)"
    data->totalDistCalLabel = GTK_LABEL(gtk_label_new("Total distance: 0 m  (counts calculated: 0   1: 0   2: 0)"));
    gtk_widget_set_halign(GTK_WIDGET(data->totalDistCalLabel), GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(leftBox), GTK_WIDGET(data->totalDistCalLabel), FALSE, FALSE, 0);
    
    gtk_box_pack_start(GTK_BOX(leftBox), gtk_label_new(""), FALSE, FALSE, 0);
    
    // Row 2: Input field
    GtkWidget* inputRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(leftBox), inputRow, FALSE, FALSE, 0);
    
    GtkWidget* inputLabel = gtk_label_new("Actual distance covered:");
    data->rallyDistEntry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(data->rallyDistEntry, "500-100000");
    gtk_widget_set_size_request(GTK_WIDGET(data->rallyDistEntry), 150, -1);
    g_signal_connect(data->rallyDistEntry, "focus-in-event", G_CALLBACK(on_entry_focus), data);
    GtkWidget* unitLabel = gtk_label_new("meters");
    
    GtkWidget* resetBtn = gtk_button_new_with_label("reset to 1m per pulse");
    g_signal_connect(resetBtn, "clicked", G_CALLBACK(on_reset_calibration_1m), data);

    gtk_box_pack_start(GTK_BOX(inputRow), inputLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(inputRow), GTK_WIDGET(data->rallyDistEntry), FALSE, FALSE, 10);
    gtk_box_pack_start(GTK_BOX(inputRow), unitLabel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(inputRow), resetBtn, FALSE, FALSE, 20);
    
    gtk_box_pack_start(GTK_BOX(leftBox), gtk_label_new(""), FALSE, FALSE, 0);
    
    // Row 3: Sensor mode selection
    GtkWidget* sensorRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_box_pack_start(GTK_BOX(leftBox), sensorRow, FALSE, FALSE, 0);
    
    data->sensorModeLabel = GTK_LABEL(gtk_label_new(
        data->state->counters ? "Currently set to both sensors" : "Currently set to sensor 1"));
    gtk_widget_set_halign(GTK_WIDGET(data->sensorModeLabel), GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(sensorRow), GTK_WIDGET(data->sensorModeLabel), FALSE, FALSE, 0);
    
    GtkWidget* sensor1Btn = gtk_button_new_with_label("Set sensor 1");
    g_signal_connect(sensor1Btn, "clicked", G_CALLBACK(on_set_sensor_1), data);
    gtk_box_pack_start(GTK_BOX(sensorRow), sensor1Btn, FALSE, FALSE, 20);
    
    GtkWidget* sensorBothBtn = gtk_button_new_with_label("Set both sensors and avg.");
    g_signal_connect(sensorBothBtn, "clicked", G_CALLBACK(on_set_sensor_both), data);
    gtk_box_pack_start(GTK_BOX(sensorRow), sensorBothBtn, FALSE, FALSE, 5);
    
    // Right side: numeric keypad
    data->calibrationKeypad = createNumericKeypad(data);
    gtk_box_pack_end(GTK_BOX(data->calibrationMainBox), data->calibrationKeypad, FALSE, FALSE, 10);
    
    // Bottom: navigation buttons
    GtkWidget* buttonBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 20);
    gtk_box_pack_end(GTK_BOX(screen), buttonBox, FALSE, FALSE, 5);
    
    GtkWidget* startBtn = gtk_button_new_with_label("start");
    GtkWidget* saveBtn = gtk_button_new_with_label("save");
    GtkWidget* backBtn = gtk_button_new_with_label("back");
    
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
