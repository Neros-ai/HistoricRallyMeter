#include "callbacks.h"
#include "webserver/rally_web_server.h"
#include "rally_types.h"
#include "rally_state.h"
#include "config_file.h"
#include "calculations.h"
#include "ui_driver.h"
#include "ui_copilot.h"
#include "ui_control.h"
#include "counter_poller.h"
#include "tone_generator.h"
#include <cmath>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstring>
#include <chrono>
#include <functional>
#include <string>
#include <stdexcept>

gboolean on_window_delete(G_GNUC_UNUSED GtkWidget* widget, G_GNUC_UNUSED GdkEvent* event, G_GNUC_UNUSED gpointer user_data) {
    gtk_main_quit();
    return FALSE;
}

void on_unit_toggle(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    data->state->units = !data->state->units;
    ConfigFile::save(*data->state);
    if (data->state->units) {
        gtk_button_set_label(data->unitToggleBtn, "MPH");
        gtk_label_set_text(data->unitsLabel, "MPH");
    } else {
        gtk_button_set_label(data->unitToggleBtn, "KPH");
        gtk_label_set_text(data->unitsLabel, "KPH");
    }
}

static void notifyWebState(AppData* data) {
    if (data && data->webServer) webNotifyStateChanged(data);
}

void on_total_reset(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    auto current_poll = data->poller->getMostRecent();
    data->state->total_start_cntr1 = current_poll.cntr1;
    data->state->total_start_cntr2 = current_poll.cntr2;
    data->state->total_start_time_ms = getRallyTime_ms(*data->state);
    // The correction described the old baseline; carrying it across a reset
    // would silently offset a counter the operator just zeroed.
    data->state->total_distance_adjust_cm = 0;
    // Waypoints are measured from the Total counter's zero, so re-zeroing it
    // puts every waypoint back in front of the car.
    data->beepNextNavIndex = 0;
    data->beepNextTimingIndex = 0;
    data->beepCursorsStale = true;
    ConfigFile::save(*data->state);
    notifyWebState(data);
}

void on_trip_reset(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    auto current_poll = data->poller->getMostRecent();
    data->state->trip_start_cntr1 = current_poll.cntr1;
    data->state->trip_start_cntr2 = current_poll.cntr2;
    data->state->trip_start_time_ms = getRallyTime_ms(*data->state);
    // The correction described the old baseline; carrying it across a reset
    // would silently offset a counter the operator just zeroed.
    data->state->trip_distance_adjust_cm = 0;
    ConfigFile::save(*data->state);
    notifyWebState(data);
}

static void applyDialogStyle(GtkWidget* dialog) {
    GtkCssProvider* provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider,
        "* { font-size: 30px; }"
        "dialog { border: 3px solid white; }"
        "button { margin-left: 10px; margin-right: 10px; }", -1, nullptr);
    
    std::function<void(GtkWidget*)> apply = [&](GtkWidget* w) {
        gtk_style_context_add_provider(
            gtk_widget_get_style_context(w),
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 100);
        if (GTK_IS_CONTAINER(w)) {
            gtk_container_forall(GTK_CONTAINER(w),
                [](GtkWidget* child, gpointer data) {
                    auto& fn = *static_cast<std::function<void(GtkWidget*)>*>(data);
                    fn(child);
                }, &apply);
        }
    };
    apply(dialog);
    g_object_unref(provider);
    
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkWidget* action_area = gtk_dialog_get_action_area(GTK_DIALOG(dialog));
    G_GNUC_END_IGNORE_DEPRECATIONS
    if (action_area && GTK_IS_BUTTON_BOX(action_area)) {
        gtk_box_set_spacing(GTK_BOX(action_area), 20);
        gtk_button_box_set_layout(GTK_BUTTON_BOX(action_area), GTK_BUTTONBOX_CENTER);
    }
}

// Raw, uncorrected distance in centimetres for one of the two counters.
// Shared by both correction callbacks so they clamp against the same figure
// the display is derived from.
static long rawDistanceCm(AppData* data, bool is_trip) {
    auto poll = data->poller->getMostRecent();
    int64_t counts = is_trip
        ? calculateDistanceCounts(*data->state, poll.cntr1, poll.cntr2,
                                  data->state->trip_start_cntr1, data->state->trip_start_cntr2)
        : calculateDistanceCounts(*data->state, poll.cntr1, poll.cntr2,
                                  data->state->total_start_cntr1, data->state->total_start_cntr2);
    return countsToCentimeters(counts, data->state->calibration);
}

void on_distance_adjust(GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    long delta_cm = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "delta_m")) * 100L;

    // Both counters share the same underlying wheel measurement, so a
    // wheel-slip correction has to apply to both -- otherwise Trip would
    // keep showing the slipped, uncorrected value. Clamped independently:
    // Total and Trip can have travelled different distances at this instant,
    // so a -10 that is safe for one could still drive the other negative.
    long& total_adjust = data->state->total_distance_adjust_cm;
    total_adjust += clampDistanceAdjust(rawDistanceCm(data, false) + total_adjust, delta_cm);

    long& trip_adjust = data->state->trip_distance_adjust_cm;
    trip_adjust += clampDistanceAdjust(rawDistanceCm(data, true) + trip_adjust, delta_cm);

    ConfigFile::save(*data->state);
    updateCopilotDisplay(data);
}

void on_distance_set(GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);

    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Set Total Distance",
        GTK_WINDOW(gtk_widget_get_toplevel(widget)),
        GTK_DIALOG_MODAL,
        "Set", GTK_RESPONSE_OK,
        "Cancel", GTK_RESPONSE_CANCEL,
        nullptr);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 10);

    GtkWidget* promptRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(content), promptRow, FALSE, FALSE, 3);

    GtkWidget* prompt = gtk_label_new("Set Total (m)");
    gtk_box_pack_start(GTK_BOX(promptRow), prompt, FALSE, FALSE, 0);

    GtkEntry* entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_text(entry,
        std::to_string(adjustedDistanceMeters(rawDistanceCm(data, false),
                                              data->state->total_distance_adjust_cm)).c_str());
    // Capped at 6 digits (999,999 m): past that the display auto-switches
    // to km, which reads as if the entered value got truncated.
    gtk_entry_set_max_length(entry, 6);
    // Pre-filled with the current value; select it so the first digit typed
    // replaces it instead of appending onto it.
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    gtk_box_pack_start(GTK_BOX(promptRow), GTK_WIDGET(entry), TRUE, TRUE, 0);

    // The main screen has no keypad of its own, and the box has no physical
    // keyboard, so the dialog brings one with it.
    GtkEntry* previous_entry = data->activeEntry;
    data->activeEntry = entry;
    gtk_box_pack_start(GTK_BOX(content), createNumericKeypad(data), FALSE, FALSE, 3);

    applyDialogStyle(dialog);
    gtk_widget_show_all(dialog);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char* text = gtk_entry_get_text(entry);
        try {
            long wanted_cm = static_cast<long>(std::stod(text) * 100.0);
            if (wanted_cm >= 0) {
                // Solve for the correction that makes the reading equal what
                // the operator entered. Trip's correction is untouched --
                // "set" has no Trip equivalent.
                long raw_cm = rawDistanceCm(data, false);
                data->state->total_distance_adjust_cm = wanted_cm - raw_cm;
                ConfigFile::save(*data->state);
            }
        } catch (const std::exception&) {
            // Unparseable entry: leave the correction as it was rather than
            // zeroing a good one on a typo.
        }
    }

    data->activeEntry = previous_entry;
    gtk_widget_destroy(dialog);
    updateCopilotDisplay(data);
}

// Every segment's count-based fields re-derived from its stable human values
// after a change to calibration or sensor mode. Covers the snapshot the
// running stage is being judged against as well as the editable roadbook and
// the memory slots -- a mid-stage calibration change that fixed up only the
// editable copy would leave the stage running on counts derived from the old
// figure, which is exactly when a calibration gets corrected.
static void recalculateSegmentCounts(RallyState& state) {
    auto redo = [&state](std::vector<Segment>& segs) {
        for (auto& seg : segs) {
            seg.target_speed_counts_per_hour =
                kphToCountsPerHour(seg.target_speed_kph, state.calibration);
            seg.distance_counts = (seg.distance_m * 1e6) / state.calibration;
        }
    };
    redo(state.segments);
    redo(state.stage_segments);
    for (int i = 0; i < RallyState::MAX_MEMORY_SLOTS; i++) {
        redo(state.memory_slots[i].segments);
    }
}

void zeroDistanceBaselines(AppData* data) {
    auto current_poll = data->poller->getMostRecent();

    data->state->total_start_cntr1 = current_poll.cntr1;
    data->state->total_start_cntr2 = current_poll.cntr2;
    data->state->trip_start_cntr1 = current_poll.cntr1;
    data->state->trip_start_cntr2 = current_poll.cntr2;
    data->state->segment_start_cntr1 = current_poll.cntr1;
    data->state->segment_start_cntr2 = current_poll.cntr2;

    // Both counters restart here, so both corrections describe a baseline
    // that no longer exists.
    data->state->total_distance_adjust_cm = 0;
    data->state->trip_distance_adjust_cm = 0;

    // Waypoints are measured from the Total counter's zero, so re-zeroing it
    // puts every distance waypoint back in front of the car. Navigation beeps
    // run from this moment; timing beeps run from the clock zero instead, and
    // cannot fire until there is a stage to be on schedule for.
    data->beepNextNavIndex = 0;
    data->beepNextTimingIndex = 0;
    data->beepCursorsStale = true;
}

int64_t autoStartRemaining_ms(const AppData* data) {
    if (data->state->auto_start_rally_time_s == 0) return 0;
    return autoStartTargetMsFromSeconds(data->state->auto_start_rally_time_s,
                                        getAutoStartEpochMs())
           - getRallyTime_ms(*data->state);
}

bool autoStartHoldActive(const AppData* data) {
    return autoStartHoldsTimeError(data->state->auto_start_rally_time_s,
                                   data->state->auto_start_early_departure,
                                   data->autoStartTriggered,
                                   autoStartRemaining_ms(data));
}

void adoptRoadbookIfIdle(AppData* data) {
    // Called wherever the roadbook is edited or replaced. While a stage is
    // under way this does nothing: that stage is judged against the segments
    // it started on, and an edit is preparation for the next one. With no
    // stage under way there is nothing to protect, so the change is adopted
    // at once -- the crew set a speed, or recall a slot, and see it.
    if (!data->state->stage_complete) return;
    data->state->stage_segments = data->state->segments;
    data->state->segment_current_number =
        data->state->stage_segments.empty() ? -1 : 0;
}

void zeroTimeBaselines(AppData* data) {
    int64_t current_time = getRallyTime_ms(*data->state);

    data->state->total_start_time_ms = current_time;
    data->state->trip_start_time_ms = current_time;
    data->state->segment_start_time_ms = current_time;

    // The stage becomes active with the CLOCK, not with the distance. Timing
    // beeps and ahead/behind are both gated on this, so an early departure
    // stays silent on schedule until the appointed minute even though it is
    // already accumulating distance.
    // The stage starts here, so this is where the roadbook it will be judged
    // against is fixed. An empty roadbook leaves no segment current, rather
    // than leaving the previous stage's index pointing into a snapshot that
    // no longer has anything at it.
    data->state->stage_segments = data->state->segments;
    data->state->segment_current_number =
        data->state->stage_segments.empty() ? -1 : 0;
    // A stage is under way from here, so the roadbook is frozen until it is
    // driven out. An empty roadbook is over before it began.
    data->state->stage_complete = data->state->stage_segments.empty();

    data->aheadBehindSeconds = 0.0;
    data->smoothedSpeed = -1.0;
    data->state->ahead_behind_zero_offset_ms = 0;
    // The timing bookmark is derived from elapsed stage time, which just
    // became zero.
    data->beepCursorsStale = true;

    if (data->toneGen) data->toneGen->setCadence(0, 0, 0.0);
}

void applyAutoStartArming(AppData* data, const AutoStartArming& arming) {
    // Assignment, not a set of conditional updates: whatever kind of
    // autostart was armed before -- ordinary, on-the-minute, even one already
    // armed for this very same target time -- is completely replaced here.
    data->state->auto_start_rally_time_s = arming.rally_time_s;
    data->state->auto_start_early_departure = arming.early_departure;
    data->autoStartTriggered = arming.triggered;
    if (arming.zero_distance_now) zeroDistanceBaselines(data);

    ConfigFile::save(*data->state);
    notifyWebState(data);
}

void performAutoStart(AppData* data) {
    if (data->state->auto_start_early_departure) {
        // Distance was zeroed when the operator armed this, so only the clock
        // starts now and everything rolled on the way to the line counts
        // toward the stage distance -- and therefore toward its average speed.
        zeroTimeBaselines(data);
        // Spent. Leaving the target armed would let a restart of the app --
        // which forgets autoStartTriggered -- see it as pending again.
        data->state->auto_start_rally_time_s = 0;
        data->state->auto_start_early_departure = false;
        ConfigFile::save(*data->state);
        notifyWebState(data);
        return;
    }
    // Ordinary autostart: distance and clock both zero at the appointed time.
    performStageGo(data);
}

void performStageGo(AppData* data) {
    // Distance and clock together: this is the ordinary "start now" case.
    zeroDistanceBaselines(data);
    zeroTimeBaselines(data);
    // Deliberately does NOT clear autoStartTriggered: the driver display sets
    // that flag just before calling this on the autostart tick, and clearing
    // it here would let the same 2-second trigger window re-enter on every
    // 10 ms frame -- re-zeroing the baselines and rewriting the config file
    // a couple of hundred times. Only the arming/clearing paths own the flag.
    //
    // The pending autostart is cancelled outright. Reached two ways, and both
    // want that: an autostart that has just fired is spent, and a manual
    // "Now" must not leave a target armed to silently re-zero the stage a
    // minute after the crew started it by hand.
    data->state->auto_start_rally_time_s = 0;
    data->state->auto_start_early_departure = false;

    ConfigFile::save(*data->state);
    notifyWebState(data);
}

static const int RESPONSE_AUTO_START = 99;
static const int RESPONSE_AUTO_START_NEXT_MINUTE = 98;

// Defined further down (used by on_show_autostart/on_autostart_set); forward
// declared here so the quick-set button below can share the same epoch.
int64_t getAutoStartEpochMs();

// Always the *next* minute boundary, even if rally_ms already sits exactly on
// one -- the quick-set button's printed time must still be ahead when pressed.
static int64_t nextRoundMinute_ms(int64_t rally_ms) {
    return ((rally_ms / 60000) + 1) * 60000;
}

static std::string formatHms(int64_t epoch_ms) {
    time_t s = epoch_ms / 1000;
    struct tm* t = localtime(&s);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
    return std::string(buf);
}

// Commits the autostart time directly (no manual entry needed), reusing the
// same state field and epoch as the "Set Autostart" screen.
static void setAutoStartToNextRoundMinute(AppData* data) {
    // Early departure: distance zeroes NOW, the clock zeroes at the minute.
    // Anything rolled between the two therefore counts toward the stage
    // distance and so toward the average speed, which is the point -- the car
    // leaves before its due time and the roadbook still starts at the minute.
    int64_t target_ms = nextRoundMinute_ms(getRallyTime_ms(*data->state));
    applyAutoStartArming(data, autoStartArming(target_ms, getAutoStartEpochMs(), true));
}

void on_stage_go(GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);

    std::string nextMinuteLabel = "Autostart "
        + formatHms(nextRoundMinute_ms(getRallyTime_ms(*data->state)));

    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Confirm Stage Go",
        GTK_WINDOW(gtk_widget_get_toplevel(widget)),
        GTK_DIALOG_MODAL,
        // "Now" rather than "Yes": what distinguishes this from the autostart
        // option is that the stage begins immediately.
        "Now", GTK_RESPONSE_YES,
        "Set Autostart", RESPONSE_AUTO_START,
        nextMinuteLabel.c_str(), RESPONSE_AUTO_START_NEXT_MINUTE,
        "No", GTK_RESPONSE_NO,
        nullptr);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 8);

    // Show what is actually about to start, highlighted: this dialog guards
    // the most destructive action on the box, and an operator who recalled
    // the wrong memory slot or mis-edited a segment had no way to notice
    // before confirming. The summary is the segments page's own content --
    // there is no separate stage name or number in the model to show
    // instead.
    std::string summary = stageSummary(data->state->segments);
    std::string markup = "Stage Go?\n\nStage = <span foreground=\"#FFDD00\">"
                       + summary + "</span>\n"
                       + "Total and Trip Counters will reset";

    GtkWidget* label = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(label), markup.c_str());
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    // A stage with many segments would otherwise force the dialog as wide
    // as the whole comma-separated list on one line; wrap it instead.
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    // width_chars, not just max_width_chars: fixing the measurement width
    // up front gives GTK a single, stable wrap pass. max_width_chars alone
    // only caps the natural width, so the dialog's first size-negotiation
    // pass (before the final width is settled) can wrap taller than the
    // final render needs, leaving reserved-but-unused height below the text.
    gtk_label_set_width_chars(GTK_LABEL(label), 44);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 44);
    // expand=FALSE: with TRUE the label was stretched to fill whatever
    // extra height the button row's width forced onto the dialog, and its
    // 0.5 yalign centred the text in that slack -- an equal gap above
    // "Stage Go?" and below the last line, both growing every time
    // wrapping added another line. Natural size only, no slack to centre in.
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);
    gtk_widget_show(label);
    
    applyDialogStyle(dialog);
    
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (response == GTK_RESPONSE_YES) {
        performStageGo(data);
    } else if (response == RESPONSE_AUTO_START) {
        on_show_autostart(widget, user_data);
    } else if (response == RESPONSE_AUTO_START_NEXT_MINUTE) {
        // Already fully committed by setAutoStartToNextRoundMinute -- no
        // Set/Save step needed, so just return to whatever screen was
        // showing rather than detouring through the autostart setup screen.
        setAutoStartToNextRoundMinute(data);
    }
}

static const int RESPONSE_RESET_ZERO = 100;

void on_adj_driver_zero(GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    
    double current_ahead_behind = data->aheadBehindSeconds;
    double current_offset_s = data->state->ahead_behind_zero_offset_ms / 1000.0;
    
    char msg[256];
    snprintf(msg, sizeof(msg),
        "Adjust the ahead/behind value\nby %.2f seconds,\ncurrently %.2f seconds",
        current_ahead_behind, current_offset_s);
    
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Adjust Driver Zero",
        GTK_WINDOW(gtk_widget_get_toplevel(widget)),
        GTK_DIALOG_MODAL,
        "Yes", GTK_RESPONSE_YES,
        "No", GTK_RESPONSE_NO,
        nullptr);
    
    GtkWidget* resetBtn = gtk_dialog_add_button(GTK_DIALOG(dialog), "Reset to 0.0", RESPONSE_RESET_ZERO);
    gtk_widget_set_margin_start(resetBtn, 40);
    
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 20);
    
    GtkWidget* label = gtk_label_new(msg);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(content), label, TRUE, TRUE, 10);
    gtk_widget_show(label);
    
    applyDialogStyle(dialog);
    
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    
    if (response == GTK_RESPONSE_YES) {
        long offset_ms = static_cast<long>(current_ahead_behind * -1000.0);
        data->state->ahead_behind_zero_offset_ms += offset_ms;
        ConfigFile::save(*data->state);
    } else if (response == RESPONSE_RESET_ZERO) {
        data->state->ahead_behind_zero_offset_ms = 0;
        ConfigFile::save(*data->state);
    }
}

void on_next_segment(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    if (data->state->segment_current_number < static_cast<long>(data->state->stage_segments.size()) - 1) {
        auto current_poll = data->poller->getMostRecent();
        data->state->segment_current_number++;
        data->state->segment_start_cntr1 = current_poll.cntr1;
        data->state->segment_start_cntr2 = current_poll.cntr2;
        data->state->segment_start_time_ms = getRallyTime_ms(*data->state);
        // Reset trip
        data->state->trip_start_cntr1 = current_poll.cntr1;
        data->state->trip_start_cntr2 = current_poll.cntr2;
        data->state->trip_start_time_ms = data->state->segment_start_time_ms;
        ConfigFile::save(*data->state);
    }
}

void on_next_prev_segment(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    if (data->state->segment_current_number < 0 ||
        data->state->segment_current_number >= static_cast<long>(data->state->stage_segments.size()))
        return;
    
    auto current_poll = data->poller->getMostRecent();
    int64_t seg_count_diff = calculateDistanceCounts(*data->state,
        current_poll.cntr1, current_poll.cntr2,
        data->state->segment_start_cntr1, data->state->segment_start_cntr2);
    
    // The snapshot, not the saved roadbook: "next"/"prev" retime the stage
    // actually being driven, and must not rewrite the roadbook the crew will
    // start the next stage from.
    Segment& cur_seg = data->state->stage_segments[data->state->segment_current_number];
    int64_t remaining_counts = cur_seg.distance_counts - seg_count_diff;
    long remaining_m = countsToCentimeters(remaining_counts, data->state->calibration) / 100;
    long travelled_m = countsToCentimeters(seg_count_diff, data->state->calibration) / 100;
    
    long next_seg_idx = data->state->segment_current_number + 1;
    bool near_end = (remaining_m >= 0 && remaining_m <= 500) &&
                    (next_seg_idx < static_cast<long>(data->state->stage_segments.size()));
    bool near_start = (travelled_m >= 0 && travelled_m <= 500) &&
                      (data->state->segment_current_number > 0);
    
    if (near_end) {
        // "next": reduce current segment distance to actual distance travelled, then advance
        cur_seg.distance_counts = seg_count_diff;
        cur_seg.distance_m = (cur_seg.distance_counts * data->state->calibration) / 1e6;
        
        data->state->segment_current_number++;
        data->state->segment_start_cntr1 = current_poll.cntr1;
        data->state->segment_start_cntr2 = current_poll.cntr2;
        data->state->segment_start_time_ms = getRallyTime_ms(*data->state);
        data->state->trip_start_cntr1 = current_poll.cntr1;
        data->state->trip_start_cntr2 = current_poll.cntr2;
        data->state->trip_start_time_ms = data->state->segment_start_time_ms;
    } else if (near_start) {
        // "prev": extend previous segment distance to end here, reset current segment start to now
        Segment& prev_seg = data->state->stage_segments[data->state->segment_current_number - 1];
        prev_seg.distance_counts += seg_count_diff;
        prev_seg.distance_m = (prev_seg.distance_counts * data->state->calibration) / 1e6;
        
        data->state->segment_start_cntr1 = current_poll.cntr1;
        data->state->segment_start_cntr2 = current_poll.cntr2;
        data->state->segment_start_time_ms = getRallyTime_ms(*data->state);
        data->state->trip_start_cntr1 = current_poll.cntr1;
        data->state->trip_start_cntr2 = current_poll.cntr2;
        data->state->trip_start_time_ms = data->state->segment_start_time_ms;
    }
    
    ConfigFile::save(*data->state);
    notifyWebState(data);
}

gboolean update_display(gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    
    // Poll counters (respects 5ms minimum interval)
    data->poller->poll(data->counter1, data->counter2, data->register_addr);
    
    // The stage's own distance is what ends it. Once it is driven out the
    // roadbook stops being frozen, so the next edit or recall shows up
    // straight away -- the ahead/behind figure at the line is left alone
    // until the crew actually change something.
    if (!data->state->stage_complete) {
        auto poll = data->poller->getMostRecent();
        int64_t stage_counts = calculateDistanceCounts(*data->state,
            poll.cntr1, poll.cntr2,
            data->state->total_start_cntr1, data->state->total_start_cntr2);
        if (stageDistanceComplete(data->state->stage_segments, stage_counts)) {
            data->state->stage_complete = true;
            ConfigFile::save(*data->state);
        }
    }

    // Check for auto-advance segments
    if (data->state->segment_current_number >= 0 && 
        data->state->segment_current_number < static_cast<long>(data->state->stage_segments.size())) {
        Segment& seg = data->state->stage_segments[data->state->segment_current_number];
        if (seg.autoNext) {
            auto current_poll = data->poller->getMostRecent();
            int64_t seg_count_diff = calculateDistanceCounts(*data->state,
                current_poll.cntr1, current_poll.cntr2,
                data->state->segment_start_cntr1, data->state->segment_start_cntr2);
            
            if (seg_count_diff >= seg.distance_counts) {
                // Advance to next segment
                if (data->state->segment_current_number < static_cast<long>(data->state->stage_segments.size()) - 1) {
                    data->state->segment_current_number++;
                    auto current_poll = data->poller->getMostRecent();
                    data->state->segment_start_cntr1 = current_poll.cntr1;
                    data->state->segment_start_cntr2 = current_poll.cntr2;
                    data->state->segment_start_time_ms = getRallyTime_ms(*data->state);
                    // Reset trip
                    data->state->trip_start_cntr1 = current_poll.cntr1;
                    data->state->trip_start_cntr2 = current_poll.cntr2;
                    data->state->trip_start_time_ms = data->state->segment_start_time_ms;
                    ConfigFile::save(*data->state);
                    notifyWebState(data);
                }
            }
        }
    }
    
    updateDriverDisplay(data);
    updateCopilotDisplay(data);
    updateControlDisplay(data);

    if (data->webServer) {
        auto now = std::chrono::system_clock::now();
        int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        data->webServer->poll(now_ms);
    }
    
    return G_SOURCE_CONTINUE;
}

// Screen navigation callbacks
void on_show_segments(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    releaseKeypadTarget(data);
    gtk_stack_set_visible_child_name(data->copilotStack, "stagesetup");
    // Refresh segment list
    refreshSegmentList(data);
}

void on_show_calibration(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    releaseKeypadTarget(data);
    gtk_stack_set_visible_child_name(data->copilotStack, "calibration");
    
    // Reset calibration state when entering screen
    data->cal_started = false;
    data->activeEntry = data->rallyDistEntry;  // Set active entry for keypad
    
    // Update calibration display
    updateCalibrationDisplay(data);
}

void on_show_twinmaster(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    releaseKeypadTarget(data);
    gtk_stack_set_visible_child_name(data->copilotStack, "twinmaster");
}

void on_show_datetime(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    releaseKeypadTarget(data);
    gtk_stack_set_visible_child_name(data->copilotStack, "datetime");
    updateDateTimeDisplay(data);

    int64_t rally_ms = getRallyTime_ms(*data->state);
    time_t rally_seconds = rally_ms / 1000;
    struct tm* rally_tm = localtime(&rally_seconds);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d/%02d/%02d",
             rally_tm->tm_year + 1900, rally_tm->tm_mon + 1, rally_tm->tm_mday);
    gtk_entry_set_text(data->dateEntry, buf);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             rally_tm->tm_hour, rally_tm->tm_min, rally_tm->tm_sec);
    gtk_entry_set_text(data->timeEntry, buf);
}

// Create numeric keypad widget
GtkWidget* createNumericKeypad(AppData* data) {
    GtkWidget* keypad = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(keypad), 5);
    gtk_grid_set_column_spacing(GTK_GRID(keypad), 5);
    
    const char* digits[] = {"7", "8", "9", "4", "5", "6", "1", "2", "3", ";", "0", "."};
    
    for (int i = 0; i < 12; i++) {
        GtkWidget* btn = gtk_button_new_with_label(digits[i]);
        gtk_widget_set_size_request(btn, 60, 42);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_keypad_digit), data);
        gtk_grid_attach(GTK_GRID(keypad), btn, i % 3, i / 3, 1, 1);
    }
    
    // Row 5: Clear and Backspace
    GtkWidget* clearBtn = gtk_button_new_with_label("C");
    gtk_widget_set_size_request(clearBtn, 60, 42);
    g_signal_connect(clearBtn, "clicked", G_CALLBACK(on_keypad_clear), data);
    gtk_grid_attach(GTK_GRID(keypad), clearBtn, 0, 4, 1, 1);

    GtkWidget* bkspBtn = gtk_button_new_with_label("<-");
    gtk_widget_set_size_request(bkspBtn, 130, 42);
    g_signal_connect(bkspBtn, "clicked", G_CALLBACK(on_keypad_backspace), data);
    gtk_grid_attach(GTK_GRID(keypad), bkspBtn, 1, 4, 2, 1);
    
    return keypad;
}

GtkWidget* createDateTimeKeypad(AppData* data) {
    GtkWidget* keypad = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(keypad), 5);
    gtk_grid_set_column_spacing(GTK_GRID(keypad), 5);
    
    const char* digits[] = {"7", "8", "9", "4", "5", "6", "1", "2", "3", "/", "0", ":"};
    
    for (int i = 0; i < 12; i++) {
        GtkWidget* btn = gtk_button_new_with_label(digits[i]);
        gtk_widget_set_size_request(btn, 60, 42);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_keypad_digit), data);
        gtk_grid_attach(GTK_GRID(keypad), btn, i % 3, i / 3, 1, 1);
    }
    
    GtkWidget* clearBtn = gtk_button_new_with_label("C");
    gtk_widget_set_size_request(clearBtn, 60, 42);
    g_signal_connect(clearBtn, "clicked", G_CALLBACK(on_keypad_clear), data);
    gtk_grid_attach(GTK_GRID(keypad), clearBtn, 0, 4, 1, 1);

    GtkWidget* bkspBtn = gtk_button_new_with_label("<-");
    gtk_widget_set_size_request(bkspBtn, 130, 42);
    g_signal_connect(bkspBtn, "clicked", G_CALLBACK(on_keypad_backspace), data);
    gtk_grid_attach(GTK_GRID(keypad), bkspBtn, 1, 4, 2, 1);
    
    return keypad;
}

// Keypad callbacks
void on_keypad_digit(GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    if (data->activeBuffer) {
        gtk_text_buffer_insert_at_cursor(data->activeBuffer,
                                         gtk_button_get_label(GTK_BUTTON(widget)), -1);
        return;
    }
    if (!data->activeEntry) return;

    const char* digit = gtk_button_get_label(GTK_BUTTON(widget));

    // A digit typed while the entry's text is selected (e.g. the current
    // value a dialog pre-fills on open) replaces it, matching normal
    // text-field behaviour -- otherwise the typed digits silently append
    // to whatever was left in the box, inflating the result.
    gint sel_start, sel_end;
    if (gtk_editable_get_selection_bounds(GTK_EDITABLE(data->activeEntry), &sel_start, &sel_end)) {
        gtk_editable_delete_selection(GTK_EDITABLE(data->activeEntry));
    }

    const char* current = gtk_entry_get_text(data->activeEntry);
    std::string new_text = std::string(current) + digit;
    gtk_entry_set_text(data->activeEntry, new_text.c_str());
}

void on_keypad_clear(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    if (data->activeBuffer) {
        gtk_text_buffer_set_text(data->activeBuffer, "", -1);
        return;
    }
    if (!data->activeEntry) return;
    gtk_entry_set_text(data->activeEntry, "");
}

void on_keypad_backspace(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    if (data->activeBuffer) {
        GtkTextIter cursor;
        gtk_text_buffer_get_iter_at_mark(data->activeBuffer, &cursor,
                                         gtk_text_buffer_get_insert(data->activeBuffer));
        gtk_text_buffer_backspace(data->activeBuffer, &cursor, TRUE, TRUE);
        return;
    }
    if (!data->activeEntry) return;

    const char* current = gtk_entry_get_text(data->activeEntry);
    std::string text(current);
    if (!text.empty()) {
        text.pop_back();
        gtk_entry_set_text(data->activeEntry, text.c_str());
    }
}

gboolean on_entry_focus(GtkWidget* widget, G_GNUC_UNUSED GdkEvent* event, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    data->activeEntry = GTK_ENTRY(widget);
    data->activeBuffer = nullptr;
    gtk_widget_show(data->numericKeypad);
    return FALSE;
}

gboolean on_textview_focus(GtkWidget* widget, G_GNUC_UNUSED GdkEvent* event, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    data->activeEntry = nullptr;   // exactly one keypad target at a time
    data->activeBuffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
    // The keypad starts hidden and is the only way to type on the box, so it
    // must be raised here exactly as on_entry_focus does -- without this,
    // tapping the waypoint list produced no keypad at all until some
    // unrelated entry had been focused first.
    if (data->numericKeypad) gtk_widget_show(data->numericKeypad);
    return FALSE;
}

// Commit any pending waypoint edit and drop the keypad's target. Called when
// leaving a screen: the keypad target is process-wide (the date/time screen
// builds its own keypad on the same handlers and the same AppData), so a
// stale target let a keypress on one screen edit a widget on another -- and
// the keypad's "C" silently wipe it.
void releaseKeypadTarget(AppData* data) {
    if (data->beepWaypointCommitTimer) {
        g_source_remove(data->beepWaypointCommitTimer);
        data->beepWaypointCommitTimer = 0;
        commitBeepWaypoints(data);
    }
    data->activeEntry = nullptr;
    data->activeBuffer = nullptr;
}

// Callback for when segment entry value changes
void on_segment_entry_changed(GtkWidget* widget, gpointer user_data) {
    int index = GPOINTER_TO_INT(user_data);
    AppData* data = static_cast<AppData*>(g_object_get_data(G_OBJECT(widget), "app_data"));
    if (!data || index < 0 || index >= static_cast<int>(data->state->segments.size())) return;
    
    const char* entry_type = static_cast<const char*>(g_object_get_data(G_OBJECT(widget), "entry_type"));
    const char* text = gtk_entry_get_text(GTK_ENTRY(widget));
    
    if (text && strlen(text) > 0) {
        if (strcmp(entry_type, "speed") == 0) {
            double kph = std::stod(text);
            data->state->segments[index].target_speed_kph = kph;
            data->state->segments[index].target_speed_counts_per_hour = kphToCountsPerHour(kph, data->state->calibration);
        } else if (strcmp(entry_type, "distance") == 0) {
            double meters = std::stod(text);
            data->state->segments[index].distance_m = meters;
            data->state->segments[index].distance_counts = (meters * 1e6) / data->state->calibration;
        // Edited, so it is no longer the slot it was recalled from.
        data->state->last_memory_slot = 0;
        adoptRoadbookIfIdle(data);
        }
        ConfigFile::save(*data->state);
        notifyWebState(data);
    }
}

// Callback for when segment auto checkbox toggled
void on_segment_auto_toggled(GtkWidget* widget, gpointer user_data) {
    int index = GPOINTER_TO_INT(user_data);
    AppData* data = static_cast<AppData*>(g_object_get_data(G_OBJECT(widget), "app_data"));
    if (!data || index < 0 || index >= static_cast<int>(data->state->segments.size())) return;
    
    data->state->segments[index].autoNext = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
    data->state->last_memory_slot = 0;
    adoptRoadbookIfIdle(data);
    ConfigFile::save(*data->state);
    notifyWebState(data);
}

// Helper function to refresh segment list with editable entries
void refreshSegmentList(AppData* data) {
    // Clear existing rows
    GList* children = gtk_container_get_children(GTK_CONTAINER(data->segmentListBox));
    for (GList* l = children; l != NULL; l = l->next) {
        gtk_widget_destroy(GTK_WIDGET(l->data));
    }
    g_list_free(children);
    
    // Add segments with editable entries
    for (size_t i = 0; i < data->state->segments.size(); i++) {
        const Segment& seg = data->state->segments[i];
        double target_kph = seg.target_speed_kph;
        long distance_m = static_cast<long>(seg.distance_m);
        
        GtkWidget* row = gtk_list_box_row_new();
        GtkWidget* grid = gtk_grid_new();
        gtk_grid_set_column_spacing(GTK_GRID(grid), SEGMENT_COL_SPACING);
        gtk_style_context_add_class(gtk_widget_get_style_context(grid), "segment-row");
        gtk_container_add(GTK_CONTAINER(row), grid);

        // Speed entry (editable)
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << target_kph;
        GtkWidget* speedEntry = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(speedEntry), ss.str().c_str());
        g_object_set_data(G_OBJECT(speedEntry), "app_data", data);
        g_object_set_data(G_OBJECT(speedEntry), "entry_type", (gpointer)"speed");
        g_signal_connect(speedEntry, "changed", G_CALLBACK(on_segment_entry_changed), GINT_TO_POINTER(i));
        g_signal_connect(speedEntry, "focus-in-event", G_CALLBACK(on_entry_focus), data);
        
        // Distance entry (editable)
        ss.str("");
        ss << distance_m;
        GtkWidget* distEntry = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(distEntry), ss.str().c_str());
        g_object_set_data(G_OBJECT(distEntry), "app_data", data);
        g_object_set_data(G_OBJECT(distEntry), "entry_type", (gpointer)"distance");
        g_signal_connect(distEntry, "changed", G_CALLBACK(on_segment_entry_changed), GINT_TO_POINTER(i));
        g_signal_connect(distEntry, "focus-in-event", G_CALLBACK(on_entry_focus), data);
        
        // Auto checkbox (editable)
        GtkWidget* autoCheck = gtk_check_button_new();
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(autoCheck), seg.autoNext);
        g_object_set_data(G_OBJECT(autoCheck), "app_data", data);
        g_signal_connect(autoCheck, "toggled", G_CALLBACK(on_segment_auto_toggled), GINT_TO_POINTER(i));
        
        // Time label (display only: mm:ss to cover distance at speed)
        int time_secs = (seg.distance_m > 0 && seg.target_speed_kph > 0)
            ? static_cast<int>(seg.distance_m / (seg.target_speed_kph * 1000.0 / 3600.0))
            : 0;
        ss.str("");
        ss << std::setw(2) << std::setfill('0') << (time_secs / 60)
           << ":" << std::setw(2) << std::setfill('0') << (time_secs % 60);
        GtkWidget* timeLabel = gtk_label_new(ss.str().c_str());
        gtk_style_context_add_class(gtk_widget_get_style_context(timeLabel), "segment-label");
        
        // Delete button
        GtkWidget* deleteBtn = gtk_button_new_with_label("del");
        g_object_set_data(G_OBJECT(deleteBtn), "app_data", data);
        g_signal_connect(deleteBtn, "clicked", G_CALLBACK(on_delete_segment), GINT_TO_POINTER(i));

        gtk_widget_set_size_request(speedEntry, SEGMENT_COL_SPEED, 36);
        gtk_grid_attach(GTK_GRID(grid), speedEntry, 0, 0, 1, 1);

        gtk_widget_set_size_request(distEntry, SEGMENT_COL_DISTANCE, 36);
        gtk_grid_attach(GTK_GRID(grid), distEntry, 1, 0, 1, 1);

        gtk_widget_set_size_request(autoCheck, SEGMENT_COL_AUTO, -1);
        gtk_grid_attach(GTK_GRID(grid), autoCheck, 2, 0, 1, 1);

        gtk_label_set_xalign(GTK_LABEL(timeLabel), 0.0);
        gtk_widget_set_size_request(timeLabel, SEGMENT_COL_TIME, -1);
        gtk_grid_attach(GTK_GRID(grid), timeLabel, 3, 0, 1, 1);

        gtk_widget_set_size_request(deleteBtn, SEGMENT_COL_DELETE, 36);
        gtk_grid_attach(GTK_GRID(grid), deleteBtn, 4, 0, 1, 1);

        gtk_list_box_insert(data->segmentListBox, row, -1);
        gtk_widget_show_all(row);
    }
}

// Callback for calibration start button
void on_calibration_start(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    auto current_poll = data->poller->getMostRecent();
    
    // Remember baseline counter values
    data->cal_start_cntr1 = current_poll.cntr1;
    data->cal_start_cntr2 = current_poll.cntr2;
    data->cal_started = true;
    
    // Update display immediately
    updateCalibrationDisplay(data);
}

// Helper function to update calibration display
void updateCalibrationDisplay(AppData* data) {
    auto current_poll = data->poller->getMostRecent();

    gtk_label_set_text(data->calibrationClockLabel,
        formatTime(getRallyTime_ms(*data->state)).c_str());

    // Calculate counts from calibration start (or total start if not started)
    uint64_t start_cntr1 = data->cal_started ? data->cal_start_cntr1 : data->state->total_start_cntr1;
    uint64_t start_cntr2 = data->cal_started ? data->cal_start_cntr2 : data->state->total_start_cntr2;
    
    // Individual counter differences
    int64_t cntr1_diff = current_poll.cntr1 - start_cntr1;
    int64_t cntr2_diff = current_poll.cntr2 - start_cntr2;
    
    // CNTR_A (calculated) - average if two counters, or just cntr1 if single
    int64_t cntr_a;
    if (data->state->counters) {
        // Two wheel counters - average
        cntr_a = (cntr1_diff + cntr2_diff) / 2;
    } else {
        // Single gearbox counter
        cntr_a = cntr1_diff;
    }
    
    long total_m = countsToCentimeters(cntr_a, data->state->calibration) / 100;
    gtk_label_set_text(data->totalDistCalLabel,
        calibrationReadoutLine(total_m, cntr_a, cntr1_diff, cntr2_diff).c_str());

    char current[96];
    snprintf(current, sizeof(current),
             "Current <span foreground=\"#FFDD00\">Calibration %ld pulses/KM</span>. Reset to",
             static_cast<long>(pulsesPerKm(data->state->calibration) + 0.5));
    gtk_label_set_markup(data->calibrationCurrentLabel, current);
}

// Helper function to update date/time display
void updateDateTimeDisplay(AppData* data) {
    // System clock
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    time_t seconds = ms / 1000;
    struct tm* tm = localtime(&seconds);
    
    char buf[100];
    snprintf(buf, sizeof(buf), "%04d/%02d/%02d",
             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    gtk_label_set_text(data->systemClockLabel, buf);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             tm->tm_hour, tm->tm_min, tm->tm_sec);
    if (data->systemTimeLabel) gtk_label_set_text(data->systemTimeLabel, buf);
    
    // Rally clock
    int64_t rally_ms = getRallyTime_ms(*data->state);
    time_t rally_seconds = rally_ms / 1000;
    struct tm* rally_tm = localtime(&rally_seconds);
    
    snprintf(buf, sizeof(buf), "%04d/%02d/%02d",
             rally_tm->tm_year + 1900, rally_tm->tm_mon + 1, rally_tm->tm_mday);
    gtk_label_set_text(data->rallyClockLabel, buf);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
             rally_tm->tm_hour, rally_tm->tm_min, rally_tm->tm_sec);
    if (data->rallyTimeLabel) gtk_label_set_text(data->rallyTimeLabel, buf);

    if (data->webUrlLabel && data->webServer && data->state->web_enabled) {
        std::string url = data->webServer->getWebUrl();
        // Drop the scheme ("http://") to keep the on-screen address compact.
        const std::string scheme = "http://";
        if (url.rfind(scheme, 0) == 0) url = url.substr(scheme.size());
        gtk_label_set_text(data->webUrlLabel, url.c_str());
        if (data->webQrArea) {
            gtk_widget_show(data->webQrArea);
            gtk_widget_queue_draw(data->webQrArea);
        }
        gtk_widget_show(GTK_WIDGET(data->webUrlLabel));
    } else if (data->webUrlLabel) {
        gtk_label_set_text(data->webUrlLabel, "(web server disabled)");
        if (data->webQrArea) gtk_widget_hide(data->webQrArea);
    }
}

void on_add_segment(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    
    const char* speed_str = gtk_entry_get_text(data->targetSpeedEntry);
    const char* dist_str = gtk_entry_get_text(data->distanceEntry);
    bool autoNext = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(data->autoNextCheck));
    
    if (speed_str && dist_str && strlen(speed_str) > 0 && strlen(dist_str) > 0) {
        // RB-SEG-03: both fields are ';'-separated lists now, not just
        // distance. A single speed still broadcasts to every distance (the
        // original use case); multiple speeds must match the distance
        // count and pair positionally -- see buildSegmentSpeedDistancePairs()
        // in calculations.cpp for the exact rule and why mismatched counts
        // are rejected outright rather than guessed at.
        std::vector<double> speeds = parseSemicolonList(speed_str);
        std::vector<double> distances = parseSemicolonList(dist_str);
        std::vector<std::pair<double, double>> pairs;
        bool paired = buildSegmentSpeedDistancePairs(speeds, distances, pairs);

        bool added = false;

        if (paired) {
            for (const auto& [target_kph, distance_m] : pairs) {
                if (distance_m <= 0) continue;

                double target_counts_per_hour = kphToCountsPerHour(target_kph, data->state->calibration);
                double distance_counts = (distance_m * 1e6) / data->state->calibration;

                Segment seg;
                seg.target_speed_kph = target_kph;
                seg.target_speed_counts_per_hour = target_counts_per_hour;
                seg.distance_m = distance_m;
                seg.distance_counts = distance_counts;
                seg.autoNext = autoNext;

                data->state->segments.push_back(seg);
                data->state->last_memory_slot = 0;
                adoptRoadbookIfIdle(data);
                added = true;
            }
        }

        if (added) {
            ConfigFile::save(*data->state);
            
            gtk_entry_set_text(data->targetSpeedEntry, "");
            gtk_entry_set_text(data->distanceEntry, "");
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(data->autoNextCheck), TRUE);
            
            refreshSegmentList(data);
            notifyWebState(data);
        }
    }
}

void on_delete_segment(GtkWidget* widget, gpointer user_data) {
    // user_data contains the segment index
    int index = GPOINTER_TO_INT(user_data);
    
    // We need to get AppData from the widget's parent hierarchy
    // For now, use a simpler approach - store AppData pointer in widget data
    AppData* data = static_cast<AppData*>(g_object_get_data(G_OBJECT(widget), "app_data"));
    if (data && index >= 0 && index < static_cast<int>(data->state->segments.size())) {
        data->state->segments.erase(data->state->segments.begin() + index);
        data->state->last_memory_slot = 0;
        adoptRoadbookIfIdle(data);
        ConfigFile::save(*data->state);
        refreshSegmentList(data);
        notifyWebState(data);
    }
}

static void updateMemoryRecallStyles(AppData* data) {
    for (int i = 0; i < 5; i++) {
        if (!data->memoryRecallBtns[i]) continue;
        GtkStyleContext* ctx = gtk_widget_get_style_context(data->memoryRecallBtns[i]);
        if (!data->state->memory_slots[i].empty()) {
            gtk_style_context_add_class(ctx, "memory-populated");
        } else {
            gtk_style_context_remove_class(ctx, "memory-populated");
        }
    }
}

void on_memory_set(GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    int slot = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "slot")) - 1;
    if (slot < 0 || slot >= RallyState::MAX_MEMORY_SLOTS) return;
    
    if (!data->state->memory_slots[slot].empty()) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Overwrite memory slot %d?", slot + 1);
        GtkWidget* dialog = gtk_dialog_new_with_buttons(
            "Confirm Overwrite",
            GTK_WINDOW(gtk_widget_get_toplevel(widget)),
            GTK_DIALOG_MODAL,
            "Yes", GTK_RESPONSE_YES,
            "No", GTK_RESPONSE_NO,
            nullptr);
        GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
        gtk_container_set_border_width(GTK_CONTAINER(content), 20);
        GtkWidget* label = gtk_label_new(msg);
        gtk_box_pack_start(GTK_BOX(content), label, TRUE, TRUE, 10);
        gtk_widget_show(label);
        applyDialogStyle(dialog);
        
        gint response = gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        if (response != GTK_RESPONSE_YES) return;
    }
    
    data->state->memory_slots[slot].segments = data->state->segments;
    // A slot is the whole stage setup, waypoints included.
    data->state->memory_slots[slot].beep_waypoints_m = data->state->beep_waypoints_m;
    data->state->memory_slots[slot].waypoints_recorded = true;
    ConfigFile::save(*data->state);
    updateMemoryRecallStyles(data);
    notifyWebState(data);
}

void on_memory_recall(GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    int slot = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "slot")) - 1;
    if (slot >= 0 && slot < RallyState::MAX_MEMORY_SLOTS && !data->state->memory_slots[slot].empty()) {
        // A running stage keeps the segments it started on; with no stage
        // under way the recalled slot is adopted immediately, so the crew
        // see the stage they have just loaded.
        data->state->segments = data->state->memory_slots[slot].segments;
        data->state->last_memory_slot = slot + 1;
        adoptRoadbookIfIdle(data);
        // A slot from a pre-Beep-Assist config has no list of its own; the
        // live one stays rather than being silently deleted.
        if (data->state->memory_slots[slot].waypoints_recorded) {
            data->state->beep_waypoints_m = data->state->memory_slots[slot].beep_waypoints_m;
        }
        // The waypoint list just changed wholesale, so the beep bookmarks
        // mean nothing; rebuild them from where the car actually is.
        data->beepCursorsStale = true;
        ConfigFile::save(*data->state);
        refreshSegmentList(data);
        refreshBeepWaypointView(data);
        notifyWebState(data);
    }
}

void on_memory_clear(GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Confirm Clear",
        GTK_WINDOW(gtk_widget_get_toplevel(widget)),
        GTK_DIALOG_MODAL,
        "Yes", GTK_RESPONSE_YES,
        "No", GTK_RESPONSE_NO,
        nullptr);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 20);
    GtkWidget* label = gtk_label_new("Clear all memory slots?");
    gtk_box_pack_start(GTK_BOX(content), label, TRUE, TRUE, 10);
    gtk_widget_show(label);
    applyDialogStyle(dialog);
    
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response != GTK_RESPONSE_YES) return;
    
    for (int i = 0; i < RallyState::MAX_MEMORY_SLOTS; i++) {
        data->state->memory_slots[i].clear();
    }
    ConfigFile::save(*data->state);
    updateMemoryRecallStyles(data);
}

void on_save_calibration(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    
    const char* dist_str = gtk_entry_get_text(data->rallyDistEntry);
    if (dist_str && strlen(dist_str) > 0) {
        long rally_distance_m = std::stol(dist_str);
        
        if (rally_distance_m >= 500 && rally_distance_m <= 100000) {
            auto current_poll = data->poller->getMostRecent();
            
            // Use calibration start values if started, otherwise use total_start
            uint64_t start_cntr1 = data->cal_started ? data->cal_start_cntr1 : data->state->total_start_cntr1;
            uint64_t start_cntr2 = data->cal_started ? data->cal_start_cntr2 : data->state->total_start_cntr2;
            
            int64_t total_count_diff = calculateDistanceCounts(*data->state,
                current_poll.cntr1, current_poll.cntr2,
                start_cntr1, start_cntr2);
            
            if (total_count_diff > 0) {
                // new_cal = (input_meters * 1000 * 1000) / total_count_diff
                data->state->calibration = (rally_distance_m * 1000000) / total_count_diff;
                
                // The sim counters were given a counts-per-second derived from
                // the OLD calibration; re-derive so the sim keeps driving at
                // the speed the control panel says it does. Preserves paused
                // state -- a calibration change must not restart the sim.
                resyncSimCounterRate(data);

                // Recalculate count-based values in all segments from stable human values
                recalculateSegmentCounts(*data->state);
                
                ConfigFile::save(*data->state);
                
                // Clear entry and reset calibration state
                gtk_entry_set_text(data->rallyDistEntry, "");
                data->cal_started = false;
                updateCalibrationDisplay(data);
            }
        }
    }
}

void on_reset_calibration_pulses(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    const char* text = gtk_entry_get_text(data->resetPulsesEntry);
    if (!text || !*text) return;

    double pulses_per_km = 0.0;
    try {
        pulses_per_km = std::stod(text);
    } catch (const std::exception&) {
        return;  // unparseable entry -- leave calibration untouched
    }

    long new_calibration = calibrationFromPulsesPerKm(pulses_per_km);
    if (new_calibration <= 0) return;  // zero/negative entry -- no-op, not a crash

    data->state->calibration = new_calibration;

    // Same re-derivation as the measured-calibration path above.
    resyncSimCounterRate(data);

    // Recalculate count-based values in all segments from stable human
    // values, same as every other calibration change.
    recalculateSegmentCounts(*data->state);

    ConfigFile::save(*data->state);
    updateCalibrationDisplay(data);
}

void updateSensorModeLabel(AppData* data) {
    // The sensor selection silently changes what every other number on this
    // screen means, so the phrase itself is highlighted in the same yellow
    // the driver panel uses for the live speed.
    const char* phrase = data->state->counters
        ? "using Sensors 1+2 (avg)"
        : "using Sensor 1";
    std::string markup = std::string("Currently <span foreground=\"#FFDD00\">")
                       + phrase + "</span>";
    gtk_label_set_markup(data->sensorModeLabel, markup.c_str());
}

void on_set_sensor_1(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    data->state->counters = false;

    recalculateSegmentCounts(*data->state);

    ConfigFile::save(*data->state);
    updateSensorModeLabel(data);
    updateCalibrationDisplay(data);
}

void on_set_sensor_both(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    data->state->counters = true;

    recalculateSegmentCounts(*data->state);

    ConfigFile::save(*data->state);
    updateSensorModeLabel(data);
    updateCalibrationDisplay(data);
}

void on_alarm_set(GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    int km = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "km"));
    
    auto current_poll = data->poller->getMostRecent();
    int64_t total_counts = calculateDistanceCounts(*data->state,
        current_poll.cntr1, current_poll.cntr2,
        data->state->total_start_cntr1, data->state->total_start_cntr2);
    
    int64_t km_in_counts = static_cast<int64_t>((static_cast<double>(km) * 1000.0 * 1e6) / data->state->calibration);
    data->state->alarm_distance_km = km;
    data->state->alarm_target_counts = total_counts + km_in_counts;
    data->alarmSoundStartTime = 0;
    
    ConfigFile::save(*data->state);
}

void on_alarm_clear(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    data->state->alarm_distance_km = 0;
    data->state->alarm_target_counts = 0;
    data->alarmSoundStartTime = 0;
    system("pkill -f 'aplay alarm.wav' 2>/dev/null");
    gtk_label_set_text(data->alarmCountdownLabel, "");
    ConfigFile::save(*data->state);
}

void on_exit_app(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    ConfigFile::save(*data->state);
    gtk_main_quit();
}

// "force single display mode" switch; saved to config, applied at next startup
gboolean on_force_single_display_toggle(G_GNUC_UNUSED GtkSwitch* sw, gboolean state,
                                        gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    data->state->force_single_display = state;
    ConfigFile::save(*data->state);
    return FALSE;  // allow default handler to update the switch visual state
}

// "tone mode off/on" switch on the Stage Setup screen -- master enable
// for the ahead/behind tone, independent of which type is selected.
// Takes effect immediately: ui_driver.cpp reads the flag fresh every
// 10ms tick, same as force_single_display's mechanism but without
// needing a restart (nothing here depends on window/monitor layout).
gboolean on_tone_enabled_toggle(G_GNUC_UNUSED GtkSwitch* sw, gboolean state,
                                gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    data->state->tone_enabled = state;
    ConfigFile::save(*data->state);
    return FALSE;  // allow default handler to update the switch visual state
}

// "Type 1[ ] 2[ ]" checkboxes on the Stage Setup screen -- mutually
// exclusive via a cross-reference on each widget (set in
// createStageSetupScreen) rather than a new AppData field pair,
// matching the "app_data"/"mode" g_object_set_data() pattern this file
// already uses (see on_beep_mode_toggled, on_segment_auto_toggled).
// Only acts when a box becomes CHECKED; unchecking the other box (which
// this triggers) is a no-op re-entry, since that call sees active=false
// and returns immediately -- no infinite loop.
void on_tone_type_toggled(GtkWidget* widget, gpointer user_data) {
    if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) return;
    AppData* data = static_cast<AppData*>(user_data);
    GtkWidget* other = GTK_WIDGET(g_object_get_data(G_OBJECT(widget), "other_type_check"));
    if (other) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(other), FALSE);
    bool is_type2 = (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "tone_type")) == 2);
    data->state->simple_tone_mode = is_type2;
    ConfigFile::save(*data->state);
}

void on_save_datetime(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    
    const char* date_str = gtk_entry_get_text(data->dateEntry);
    const char* time_str = gtk_entry_get_text(data->timeEntry);
    
    if (date_str && time_str && strlen(date_str) > 0 && strlen(time_str) > 0) {
        // Parse date (yyyy/mm/dd)
        int year, month, day, hour, min, sec;
        if (sscanf(date_str, "%d/%d/%d", &year, &month, &day) == 3 &&
            sscanf(time_str, "%d:%d:%d", &hour, &min, &sec) == 3) {
            
            struct tm tm = {};
            tm.tm_year = year - 1900;
            tm.tm_mon = month - 1;
            tm.tm_mday = day;
            tm.tm_hour = hour;
            tm.tm_min = min;
            tm.tm_sec = sec;
            tm.tm_isdst = -1;
            
            time_t rally_time = mktime(&tm);
            auto now = std::chrono::system_clock::now();
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            
            int64_t old_rally_ms = getRallyTime_ms(*data->state);
            time_t old_rally_s = old_rally_ms / 1000;
            struct tm* old_tm = localtime(&old_rally_s);
            fprintf(stderr, "[DEBUG datetime] Input: date='%s' time='%s'\n", date_str, time_str);
            fprintf(stderr, "[DEBUG datetime] Parsed: %04d/%02d/%02d %02d:%02d:%02d\n",
                    year, month, day, hour, min, sec);
            fprintf(stderr, "[DEBUG datetime] Old rally time: %04d/%02d/%02d %02d:%02d:%02d\n",
                    old_tm->tm_year+1900, old_tm->tm_mon+1, old_tm->tm_mday,
                    old_tm->tm_hour, old_tm->tm_min, old_tm->tm_sec);
            fprintf(stderr, "[DEBUG datetime] mktime result: %ld, rally_ms: %ld, now_ms: %ld\n",
                    (long)rally_time, (long)(rally_time * 1000), (long)now_ms);
            fprintf(stderr, "[DEBUG datetime] New offset: %ld ms\n",
                    (long)(rally_time * 1000 - now_ms));
            
            // Calculate offset
            int64_t rally_ms = rally_time * 1000;
            data->state->rallyTimeOffset_ms = rally_ms - now_ms;
            ConfigFile::save(*data->state);
            
            int64_t verify_ms = getRallyTime_ms(*data->state);
            time_t verify_s = verify_ms / 1000;
            struct tm* verify_tm = localtime(&verify_s);
            fprintf(stderr, "[DEBUG datetime] Verify rally time: %04d/%02d/%02d %02d:%02d:%02d\n",
                    verify_tm->tm_year+1900, verify_tm->tm_mon+1, verify_tm->tm_mday,
                    verify_tm->tm_hour, verify_tm->tm_min, verify_tm->tm_sec);
            
            // Clear entries
            gtk_entry_set_text(data->dateEntry, "");
            gtk_entry_set_text(data->timeEntry, "");
            updateDateTimeDisplay(data);
        }
    }
}

// Epoch for auto_start: 2020-01-01 00:00:00 local time
int64_t getAutoStartEpochMs() {
    struct tm epoch_tm = {};
    epoch_tm.tm_year = 120;  // 2020
    epoch_tm.tm_mon = 0;
    epoch_tm.tm_mday = 1;
    return static_cast<int64_t>(mktime(&epoch_tm)) * 1000;
}

void on_show_autostart(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    releaseKeypadTarget(data);
    gtk_stack_set_visible_child_name(data->copilotStack, "autostart");
    data->activeEntry = data->autoStartTimeEntry;
    
    if (data->state->auto_start_rally_time_s > 0) {
        int64_t target_ms = autoStartTargetMsFromSeconds(
            data->state->auto_start_rally_time_s, getAutoStartEpochMs());
        time_t target_s = target_ms / 1000;
        struct tm* t = localtime(&target_s);
        char buf[16];
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
        gtk_entry_set_text(data->autoStartTimeEntry, buf);
    } else {
        gtk_entry_set_text(data->autoStartTimeEntry, "");
    }
    
    updateAutoStartDisplay(data);
}

void updateAutoStartDisplay(AppData* data) {
    int64_t rally_ms = getRallyTime_ms(*data->state);
    time_t rally_s = rally_ms / 1000;
    struct tm* rally_tm = localtime(&rally_s);
    
    char buf[64];
    snprintf(buf, sizeof(buf), "%04d/%02d/%02d  %02d:%02d:%02d",
             rally_tm->tm_year + 1900, rally_tm->tm_mon + 1, rally_tm->tm_mday,
             rally_tm->tm_hour, rally_tm->tm_min, rally_tm->tm_sec);
    gtk_label_set_text(data->autoStartRallyClockLabel, buf);
    
    if (data->state->auto_start_rally_time_s > 0) {
        int64_t target_ms = autoStartTargetMsFromSeconds(
            data->state->auto_start_rally_time_s, getAutoStartEpochMs());
        time_t target_s = target_ms / 1000;
        struct tm* t = localtime(&target_s);
        snprintf(buf, sizeof(buf), "%04d/%02d/%02d  %02d:%02d:%02d",
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                 t->tm_hour, t->tm_min, t->tm_sec);
        gtk_label_set_text(data->autoStartTimeLabel, buf);
    } else {
        gtk_label_set_text(data->autoStartTimeLabel, "");
    }
}

void on_autostart_set(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    
    const char* time_str = gtk_entry_get_text(data->autoStartTimeEntry);
    if (!time_str || strlen(time_str) == 0) return;
    
    int hour = 0, min = 0, sec = 0;
    if (sscanf(time_str, "%d:%d:%d", &hour, &min, &sec) < 2) return;
    
    if (hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59) return;
    
    // Build target time: use today's rally date with the entered time
    int64_t rally_ms = getRallyTime_ms(*data->state);
    time_t rally_s = rally_ms / 1000;
    struct tm target_tm = *localtime(&rally_s);
    target_tm.tm_hour = hour;
    target_tm.tm_min = min;
    target_tm.tm_sec = sec;
    
    int64_t target_ms = static_cast<int64_t>(mktime(&target_tm)) * 1000;
    int64_t diff_ms = target_ms - rally_ms;
    
    if (diff_ms <= 0) {
        gtk_entry_set_text(data->autoStartTimeEntry, "Error: time in past");
        return;
    }
    if (diff_ms > 3 * 3600 * 1000) {
        gtk_entry_set_text(data->autoStartTimeEntry, "Error: >3 hours");
        return;
    }
    
    // Supersedes an on-the-minute autostart if one is armed, kind and all --
    // this one arms nothing and zeroes nothing until it fires.
    applyAutoStartArming(data, autoStartArming(target_ms, getAutoStartEpochMs(), false));
    updateAutoStartDisplay(data);
}

void on_autostart_clear(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    applyAutoStartArming(data, autoStartDisarmed());
    gtk_entry_set_text(data->autoStartTimeEntry, "");
    updateAutoStartDisplay(data);
}

// Parse the waypoint list out of the buffer and persist it. Parsing is cheap
// and forgiving (a malformed token is skipped, not fatal), so the operator is
// never made to confirm -- but it runs on a debounce, not per keystroke, for
// two reasons: a half-typed number is briefly a real waypoint ("3" en route
// to "30" is 3 km, which at 2.6 km travelled is immediately due and beeps),
// and committing per character rewrote the whole config JSON to the Pi's SD
// card on every key.
// Rewrites the waypoint box from state. Blocks the "changed" handler while it
// does: otherwise a programmatic set (memory recall) would arm the debounce
// and commit the list straight back, which is harmless but pointless churn on
// the SD card.
void refreshBeepWaypointView(AppData* data) {
    if (!data->beepWaypointBuffer) return;
    g_signal_handlers_block_by_func(data->beepWaypointBuffer,
                                    (gpointer)on_beep_waypoints_changed, data);
    gtk_text_buffer_set_text(data->beepWaypointBuffer,
        formatBeepWaypointsKm(data->state->beep_waypoints_m).c_str(), -1);
    g_signal_handlers_unblock_by_func(data->beepWaypointBuffer,
                                      (gpointer)on_beep_waypoints_changed, data);
}

void commitBeepWaypoints(AppData* data) {
    if (!data->beepWaypointBuffer) return;
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(data->beepWaypointBuffer, &start, &end);
    gchar* text = gtk_text_buffer_get_text(data->beepWaypointBuffer, &start, &end, FALSE);
    data->state->beep_waypoints_m = parseBeepWaypointsKm(text ? text : "");
    g_free(text);

    // The list changed under the cursors, so they no longer mean anything;
    // the next display tick rebuilds them from where the car actually is.
    data->beepCursorsStale = true;

    ConfigFile::save(*data->state);
}

static gboolean on_beep_waypoints_commit_timeout(gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    data->beepWaypointCommitTimer = 0;
    commitBeepWaypoints(data);
    return G_SOURCE_REMOVE;
}

void on_beep_waypoints_changed(G_GNUC_UNUSED GtkTextBuffer* buffer, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    if (data->beepWaypointCommitTimer) g_source_remove(data->beepWaypointCommitTimer);
    data->beepWaypointCommitTimer =
        g_timeout_add(BEEP_WAYPOINT_COMMIT_MS, on_beep_waypoints_commit_timeout, data);
}

gboolean on_beep_enable_toggled(G_GNUC_UNUSED GtkWidget* widget, gboolean state, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    data->state->beep_assist_enabled = state;
    // While Beep Assist was off the cursors stood still, so they now point at
    // waypoints the car passed long ago. Without this, switching on at 40 km
    // with waypoints at 5/10/15/20 km fires all four back to back.
    data->beepCursorsStale = true;
    ConfigFile::save(*data->state);
    return FALSE;  // let the switch draw the new state
}

void on_beep_mode_toggled(GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    bool active = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
    if (GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "mode")) == 1)
        data->state->beep_timing_mode = active;
    else
        data->state->beep_navigation_mode = active;
    // A mode that was off never advanced its cursor either -- same replay.
    if (active) data->beepCursorsStale = true;
    ConfigFile::save(*data->state);
}

void on_beep_advance_changed(GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    const char* text = gtk_entry_get_text(GTK_ENTRY(widget));
    bool is_seconds = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "mode")) == 1;
    double value = 0.0;
    try {
        if (text && *text) value = std::stod(text);
    } catch (const std::exception&) {
        // Mid-typing the field is briefly unparseable ("." on its own); treat
        // that as no lead-in rather than rejecting the keystroke.
        value = 0.0;
    }
    if (value < 0.0) value = 0.0;
    if (is_seconds) data->state->beep_advance_s = value;
    else            data->state->beep_advance_m = value;
    ConfigFile::save(*data->state);
}
