#include "callbacks.h"
#include "webserver/rally_web_server.h"
#include "rally_types.h"
#include "rally_state.h"
#include "config_file.h"
#include "calculations.h"
#include "ui_driver.h"
#include "ui_copilot.h"
#include "ui_control.h"
#include "nero_frame.h"
#include "ui_segments.h"
#include "debug_log.h"
#include "stage_log.h"
#include "ui_datetime.h"
#include "counter_poller.h"
#include "tone_generator.h"
#include <unistd.h>
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

static void applyDialogStyle(GtkWidget* dialog);
static void flashMessage(GtkWidget* widget, const std::string& text);

bool applyTotalReset(AppData* data, TotalResetChoice choice) {
    auto poll = data->poller->getMostRecent();
    const int64_t now_ms = getRallyTime_ms(*data->state);
    RallyState& state = *data->state;

    // No new stage can start while the question is open: with an autostart
    // armed there is no question. If the stage is driven out meanwhile, the
    // confirm is a plain idle reset below -- its clock no longer matters.

    switch (classifyTotalReset(state)) {
    case TotalResetCase::AwaitingEarlyDeparture:
        applyTotalResetAwaitingEarlyDeparture(state, poll.cntr1, poll.cntr2, now_ms);
        break;
    case TotalResetCase::AwaitingTimedStart:
        // The crew are at the line of the next stage, so whatever ran before
        // is over; the set time zeroes everything again when it fires.
        applyTotalResetToIdle(state, poll.cntr1, poll.cntr2, now_ms, true);
        break;
    case TotalResetCase::StageRunning: {
        if (choice != TotalResetChoice::ConfirmDistanceReset) {
            // The captured press is abandoned with the question. The box's
            // own Back clears it in on_total_reset; the phone's cancel
            // arrives here instead, and leaving it armed let a later confirm
            // that had not captured for itself zero the distance at a press
            // from minutes before.
            data->totalResetPending = false;
            return false;
        }
        // Zeroed where the button was pressed: the car kept rolling while the
        // crew read the question. With no press recorded, the counters now.
        const bool pressed = data->totalResetPending;
        applyDistanceResetKeepingClock(state,
            pressed ? data->totalResetCntr1 : poll.cntr1,
            pressed ? data->totalResetCntr2 : poll.cntr2,
            pressed ? data->totalResetPressMs : now_ms);
        data->totalResetPending = false;
        // The car may already be past speed changes by the time the crew
        // confirm. Auto-advance steps one segment at a time and re-bases each
        // at the car, so on its own it would start the next segment at the
        // confirm, not at its boundary: put the car straight into the segment
        // its distance from the press says it is in.
        {
            const int64_t stage_counts = stageCountsCorrected(data, poll);
            const long idx = segmentIndexAtStageCounts(state.stage_segments, stage_counts);
            if (idx > 0) {
                state.segment_current_number = idx;
                rebaseSegmentAt(state, poll.cntr1, poll.cntr2,
                    stage_counts - segmentStartStageCounts(state.stage_segments, idx));
            }
        }
        break;
    }
    case TotalResetCase::Idle:
        applyTotalResetToIdle(state, poll.cntr1, poll.cntr2, now_ms, false);
        break;
    }

    // Waypoints are measured from the Total counter's zero, so re-zeroing it
    // puts every waypoint back in front of the car.
    data->beepNextNavIndex = 0;
    data->beepNextTimingIndex = 0;
    data->beepCursorsStale = true;
    ConfigFile::save(state);
    notifyWebState(data);
    return true;
}

static const int RESPONSE_ABORT_STAGE = 102;

static const int RESPONSE_CONFIRM_DISTANCE_RESET = 103;

void captureTotalResetPress(AppData* data) {
    auto poll = data->poller->getMostRecent();
    data->totalResetCntr1 = poll.cntr1;
    data->totalResetCntr2 = poll.cntr2;
    data->totalResetPressMs = getRallyTime_ms(*data->state);
    data->totalResetPending = true;
}

void cancelTotalResetPress(AppData* data) {
    data->totalResetPending = false;
}

// Usership 6, a missed start: the stage clock is already running and the car
// has only now reached the line. Highly unusual mid-stage, so it is
// confirmed -- and the safe way out is the default one.
static TotalResetChoice askTotalResetDuringStage(GtkWidget* widget) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Stage Running",
        GTK_WINDOW(gtk_widget_get_toplevel(widget)),
        GTK_DIALOG_MODAL,
        "CONFIRM DISTANCE RESET", RESPONSE_CONFIRM_DISTANCE_RESET,
        "Back", GTK_RESPONSE_CANCEL,
        nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);

    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 8);
    GtkWidget* label = gtk_label_new(
        "A stage is running\n\n"
        "Total and Trip reset to zero from the press\n"
        "The stage clock keeps running");
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);
    gtk_widget_show(label);

    applyDialogStyle(dialog);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    return response == RESPONSE_CONFIRM_DISTANCE_RESET
        ? TotalResetChoice::ConfirmDistanceReset : TotalResetChoice::Cancel;
}

void on_total_reset(GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    TotalResetChoice choice = TotalResetChoice::Cancel;
    if (classifyTotalReset(*data->state) == TotalResetCase::StageRunning) {
        // Before asking: the zero is where the button was pressed.
        captureTotalResetPress(data);
        choice = askTotalResetDuringStage(widget);
        if (choice == TotalResetChoice::Cancel) {
            data->totalResetPending = false;
            return;
        }
    }
    applyTotalReset(data, choice);
}

void on_trip_reset(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    auto current_poll = data->poller->getMostRecent();
    // Clears the correction too: it described the old baseline, and carrying
    // it across a reset would silently offset a counter the operator just
    // zeroed.
    resetTrip(*data->state, current_poll.cntr1, current_poll.cntr2,
              getRallyTime_ms(*data->state));
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

// The correction itself, with no widget in sight, so the co-pilot's buttons
// and the phone client apply exactly the same arithmetic rather than two
// implementations that can drift apart.
void applyDistanceAdjust(AppData* data, long delta_m) {
    long delta_cm = delta_m * 100L;

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

// Pins Total to an exact roadbook figure. Trip's correction is untouched --
// "set" has no Trip equivalent. Returns false for a value the box would not
// accept, so the phone can decline it the way the dialog does.
bool applyDistanceSet(AppData* data, double meters) {
    long wanted_cm = static_cast<long>(meters * 100.0);
    if (wanted_cm < 0) return false;
    // Solve for the correction that makes the reading equal what the operator
    // entered.
    long raw_cm = rawDistanceCm(data, false);
    data->state->total_distance_adjust_cm = wanted_cm - raw_cm;
    ConfigFile::save(*data->state);
    updateCopilotDisplay(data);
    return true;
}

void on_distance_adjust(GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    applyDistanceAdjust(data,
        static_cast<long>(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "delta_m"))));
}

static void on_dialog_response_button(GtkWidget* button, gpointer user_data) {
    GtkWidget* dialog = static_cast<GtkWidget*>(user_data);
    gint response = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "response_id"));
    gtk_dialog_response(GTK_DIALOG(dialog), response);
}

void on_distance_set(GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);

    // No dialog-supplied buttons: Set/Cancel are placed beside the keypad
    // below instead of in the standard bottom action area, which on the box's
    // 400px-tall panel pushes the buttons off the bottom of the physical screen.
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Set Total Distance",
        GTK_WINDOW(gtk_widget_get_toplevel(widget)),
        GTK_DIALOG_MODAL,
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
    // keyboard, so the dialog brings one with it. Set/Cancel sit to the
    // right of the keypad (Set above Cancel) rather than below it.
    GtkEntry* previous_entry = data->activeEntry;
    data->activeEntry = entry;

    GtkWidget* keypadRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_box_pack_start(GTK_BOX(content), keypadRow, FALSE, FALSE, 3);
    gtk_box_pack_start(GTK_BOX(keypadRow), createNumericKeypad(data), FALSE, FALSE, 0);

    GtkWidget* actionCol = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_box_pack_start(GTK_BOX(keypadRow), actionCol, FALSE, FALSE, 0);

    GtkWidget* setBtn = gtk_button_new_with_label("Set");
    g_object_set_data(G_OBJECT(setBtn), "response_id", GINT_TO_POINTER(GTK_RESPONSE_OK));
    g_signal_connect(setBtn, "clicked", G_CALLBACK(on_dialog_response_button), dialog);
    gtk_box_pack_start(GTK_BOX(actionCol), setBtn, TRUE, TRUE, 0);

    GtkWidget* cancelBtn = gtk_button_new_with_label("Cancel");
    g_object_set_data(G_OBJECT(cancelBtn), "response_id", GINT_TO_POINTER(GTK_RESPONSE_CANCEL));
    g_signal_connect(cancelBtn, "clicked", G_CALLBACK(on_dialog_response_button), dialog);
    gtk_box_pack_start(GTK_BOX(actionCol), cancelBtn, TRUE, TRUE, 0);

    applyDialogStyle(dialog);
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_NONE);
    gtk_widget_show_all(dialog);

    // Anchored near the top of the co-pilot panel rather than vertically
    // centered: centering leaves less headroom below for the keypad and
    // action buttons, which is what was running off the bottom of the
    // physical screen.
    GtkWidget* parentWindow = gtk_widget_get_toplevel(widget);
    gint parent_x = 0, parent_y = 0, parent_w = 0;
    gtk_window_get_position(GTK_WINDOW(parentWindow), &parent_x, &parent_y);
    gtk_window_get_size(GTK_WINDOW(parentWindow), &parent_w, nullptr);
    gint dialog_w = 0;
    gtk_window_get_size(GTK_WINDOW(dialog), &dialog_w, nullptr);
    gtk_window_move(GTK_WINDOW(dialog), parent_x + (parent_w - dialog_w) / 2, parent_y + 10);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        const char* text = gtk_entry_get_text(entry);
        try {
            applyDistanceSet(data, std::stod(text));
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

    // Through the shared helpers rather than field by field (RB-NAV-18).
    // This function used to assign the same baselines itself, which is how it
    // came to miss the alarm: rebaseTotalDistance takes the covered distance
    // off the alarm's target as it moves the Total zero, and a hand-written
    // copy of its assignments did not. Between them the two helpers set
    // exactly what this function set before -- Total, Trip and segment
    // counters, both corrections cleared, segment_start_adjust taken after
    // the clear -- so the only behavioural change is the alarm.
    rebaseTotalDistance(*data->state, current_poll.cntr1, current_poll.cntr2);
    rebaseTripDistance(*data->state, current_poll.cntr1, current_poll.cntr2);
    // A stage start begins a new Trip history: the reading rebaseTripDistance
    // just recorded belongs to the drive to the line, not to the stage, and
    // the list fills (from blank) with the stage's own Trip resets.
    data->state->trip_history_m.clear();

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

    // No segment is current, rather than the first one. The stage baselines
    // still belong to the stage that just finished, and ahead/behind is
    // measured from those -- so making a segment current here would judge the
    // new roadbook's speeds against the old stage's clock zero and show a
    // false error of tens of minutes, needle pegged and tone sounding, until
    // Stage Go. There is no stage running, so there is nothing to be ahead or
    // behind of; the segment baselines are set by Stage Go along with the
    // stage ones, and need no attention here.
    //
    // It also makes this idempotent, which matters because the entry handlers
    // call it per keystroke: adopting the same roadbook twice now changes
    // nothing the displays can see.
    data->state->segment_current_number = -1;
}

// Fixes the roadbook the stage will be judged against, from the first segment.
// An empty roadbook leaves no segment current, rather than leaving the previous
// stage's index pointing into a snapshot that no longer has anything at it --
// and it is over before it began, so it does not freeze the editable list.
// Leaves the box with no stage running: nothing to be ahead or behind of, so
// the gauge, the chevrons and the tone all rest, and the roadbook is free for
// the crew to edit or recall against the stage they are about to start.
void endStageAsIdle(AppData* data) {
    data->state->segment_current_number = -1;
    data->state->stage_complete = true;
}

void adoptRoadbookAsStage(AppData* data) {
    data->state->stage_segments = data->state->segments;
    data->state->segment_current_number =
        data->state->stage_segments.empty() ? -1 : 0;
    data->state->stage_complete = data->state->stage_segments.empty();
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
    // The roadbook is fixed here for an ordinary start, and re-fixed
    // harmlessly for an early departure, which already took it at arming.
    adoptRoadbookAsStage(data);

    data->aheadBehindSeconds = 0.0;
    data->smoothedSpeed = -1.0;
    data->state->ahead_behind_zero_offset_ms = 0;
    // The timing bookmark is derived from elapsed stage time, which just
    // became zero.
    data->beepCursorsStale = true;

    data->currentTone = ToneCadence{};  // the phone falls silent too (RB-WEB-02)
    if (data->toneGen) data->toneGen->setCadence(0, 0, 0.0);
}

void applyAutoStartArming(AppData* data, const AutoStartArming& arming) {
    if (arming.rally_time_s != 0) data->autoSlot = data->state->last_memory_slot;
    // Assignment, not a set of conditional updates: whatever kind of
    // autostart was armed before -- ordinary, on-the-minute, even one already
    // armed for this very same target time -- is completely replaced here.
    data->state->auto_start_rally_time_s = arming.rally_time_s;
    data->state->auto_start_early_departure = arming.early_departure;
    data->autoStartTriggered = arming.triggered;
    if (arming.zero_distance_now) {
        zeroDistanceBaselines(data);
        // The roadbook moves with the distance, not with the clock. Arming an
        // early departure declares that this is the new stage -- its distance
        // starts counting from the press -- so the box has to be showing the
        // new stage's speeds from the press too. Taking the roadbook at the
        // minute instead left the driver panel on the previous stage's target
        // speed and chevrons for the whole countdown, while the odometer was
        // already running on the new one: an edit made just before arming
        // looked ignored, and re-pressing the button changed nothing because
        // the arming path never touched the roadbook at all.
        adoptRoadbookAsStage(data);
        // Paired with the segment counters zeroDistanceBaselines just moved,
        // so segment 1 measures from the same instant in both quantities.
        // The clock baselines stay put: the stage's time still starts at the
        // appointed minute, which is what makes this an EARLY departure.
        data->state->segment_start_time_ms = getRallyTime_ms(*data->state);
    }

    ConfigFile::save(*data->state);
    notifyWebState(data);
}

void performAutoStart(AppData* data) {
    const uint64_t armed_time_s = data->state->auto_start_rally_time_s;
    if (data->state->auto_start_early_departure) {
        // Distance was zeroed when the operator armed this, so only the clock
        // starts now and everything rolled on the way to the line counts
        // toward the stage distance -- and therefore toward its average speed.
        zeroTimeBaselines(data);
        // Spent. Leaving the target armed would let a restart of the app --
        // which forgets autoStartTriggered -- see it as pending again.
        data->state->auto_start_rally_time_s = 0;
        data->state->auto_start_early_departure = false;
        data->autoFiredTime_s = armed_time_s;
        ConfigFile::save(*data->state);
        notifyWebState(data);
        return;
    }
    // Ordinary autostart: distance and clock both zero at the appointed time.
    performStageGo(data);
    data->autoFiredTime_s = armed_time_s;   // after performStageGo, which drops it
}

void performStageGo(AppData* data) {
    // The moment of the press is the stage's start time, shown on Home like an
    // autostart's. (performAutoStart overwrites this with the armed moment.)
    data->autoFiredTime_s = autoStartSecondsFromTargetMs(getRallyTime_ms(*data->state),
                                                         getAutoStartEpochMs());
    data->autoSlot = data->state->last_memory_slot;
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

void performAbortStage(AppData* data) {
    // Any programmed autostart is cancelled, of either kind, inside a stage
    // or out of one (owner's ruling): ABORT STAGE means nothing is going to
    // start by itself.
    if (data->state->auto_start_rally_time_s != 0) {
        applyAutoStartArming(data, autoStartDisarmed());
    }
    // Ends the stage and nothing else: Total and Trip keep their readings,
    // and are reset with their own buttons if the crew want that.
    endStageAsIdle(data);
    // No stage, nothing to be ahead or behind of: the driver's needle goes
    // back to zero and the tone stops.
    data->aheadBehindSeconds = 0.0;
    data->currentTone = ToneCadence{};  // the phone falls silent too (RB-WEB-02)
    if (data->toneGen) data->toneGen->setCadence(0, 0, 0.0);
    ConfigFile::save(*data->state);
    notifyWebState(data);
}

// Defined further down (used by on_show_autostart/on_autostart_set); forward
// declared here so the quick-set button below can share the same epoch.
int64_t getAutoStartEpochMs();

// Always the *next* minute boundary, even if rally_ms already sits exactly on
// one -- the quick-set button's printed time must still be ahead when pressed.
static int64_t nextRoundMinute_ms(int64_t rally_ms) {
    return ((rally_ms / 60000) + 1) * 60000;
}

// Hours and minutes only: the quick-set button always lands on a whole
// minute, so its seconds would always read :00.
static std::string formatHm(int64_t epoch_ms) {
    time_t s = epoch_ms / 1000;
    struct tm* t = localtime(&s);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d", t->tm_hour, t->tm_min);
    return std::string(buf);
}

// Commits the autostart time directly (no manual entry needed), reusing the
// same state field and epoch as the "Set Autostart" screen.
static void setAutoStartToMinute(AppData* data, int64_t target_ms) {
    // Early departure: distance zeroes NOW, the clock zeroes at the minute.
    // Anything rolled between the two therefore counts toward the stage
    // distance and so toward the average speed, which is the point -- the car
    // leaves before its due time and the roadbook still starts at the minute.
    applyAutoStartArming(data, autoStartArming(target_ms, getAutoStartEpochMs(), true));
}

void notifyWebStateNow(AppData* data) { notifyWebState(data); }

void flashNotice(GtkWidget* widget, const std::string& text) { flashMessage(widget, text); }

bool confirmYesNo(GtkWidget* widget, const std::string& title, const std::string& text) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        title.c_str(), GTK_WINDOW(gtk_widget_get_toplevel(widget)), GTK_DIALOG_MODAL,
        "Yes", GTK_RESPONSE_YES, "No", GTK_RESPONSE_NO, nullptr);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 20);
    GtkWidget* label = gtk_label_new(text.c_str());
    gtk_box_pack_start(GTK_BOX(content), label, TRUE, TRUE, 10);
    gtk_widget_show(label);
    applyDialogStyle(dialog);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return response == GTK_RESPONSE_YES;
}

int64_t autoStartTargetForTimeOfDay(AppData* data, long tod_s) {
    // Today's occurrence of a rally-clock time of day, in rally milliseconds.
    const int64_t now_ms = getRallyTime_ms(*data->state);
    time_t t = now_ms / 1000;
    struct tm* lt = localtime(&t);
    if (!lt) return 0;
    const long now_tod = lt->tm_hour * 3600L + lt->tm_min * 60L + lt->tm_sec;
    return now_ms - (now_tod * 1000LL) + tod_s * 1000LL - (now_ms % 1000);
}

void startStageAtNextMinute(AppData* data) {
    setAutoStartToMinute(data, nextRoundMinute_ms(getRallyTime_ms(*data->state)));
}

std::string nextMinuteCaption(AppData* data) {
    return formatHm(nextRoundMinute_ms(getRallyTime_ms(*data->state)));
}

static gboolean closeFlashMessage(gpointer dialog) {
    // Spent: clear the id first so the destroy handler below does not try to
    // remove the source that is running now.
    g_object_set_data(G_OBJECT(dialog), "flash-timeout", nullptr);
    gtk_widget_destroy(GTK_WIDGET(dialog));
    return G_SOURCE_REMOVE;
}

// Closed some other way first (Escape, a window-manager close): cancel the
// timer, or it would later destroy a dialog that is already gone.
static void onFlashMessageDestroyed(GtkWidget* dialog, G_GNUC_UNUSED gpointer unused) {
    guint id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(dialog), "flash-timeout"));
    if (id != 0) g_source_remove(id);
}

// A short notice that closes itself: nothing to answer, and at the line the
// navigator's hands are busy.
static void flashMessage(GtkWidget* widget, const std::string& text) {
    GtkWidget* dialog = gtk_dialog_new();
    gtk_window_set_transient_for(GTK_WINDOW(dialog),
                                 GTK_WINDOW(gtk_widget_get_toplevel(widget)));
    gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER_ON_PARENT);
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 20);
    GtkWidget* label = gtk_label_new(text.c_str());
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);
    applyDialogStyle(dialog);
    gtk_widget_show_all(dialog);
    guint id = g_timeout_add(2500, closeFlashMessage, dialog);
    g_object_set_data(G_OBJECT(dialog), "flash-timeout", GUINT_TO_POINTER(id));
    g_signal_connect(dialog, "destroy", G_CALLBACK(onFlashMessageDestroyed), nullptr);
}

// One tap from the stage-go row must not end a stage: the button sits between
// the ones that start it.
static bool confirmAbortStage(GtkWidget* widget, const std::string& text) {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Abort Stage",
        GTK_WINDOW(gtk_widget_get_toplevel(widget)),
        GTK_DIALOG_MODAL,
        "ABORT STAGE", GTK_RESPONSE_YES,
        "Back", GTK_RESPONSE_NO,
        nullptr);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    gtk_container_set_border_width(GTK_CONTAINER(content), 8);
    GtkWidget* label = gtk_label_new(text.c_str());
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 0);
    gtk_widget_show(label);
    applyDialogStyle(dialog);
    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    return response == GTK_RESPONSE_YES;
}

void on_stage_go(GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);

    // Fixed when the menu opens, and armed exactly as printed. A crew who
    // want the minute after go Back and open the menu again.
    const int64_t at_minute_ms = nextRoundMinute_ms(getRallyTime_ms(*data->state));
    std::string nextMinuteLabel = "At " + formatHm(at_minute_ms);

    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        "Confirm Stage Go",
        GTK_WINDOW(gtk_widget_get_toplevel(widget)),
        GTK_DIALOG_MODAL,
        // "Now" rather than "Yes": what distinguishes this from the autostart
        // options is that the stage begins immediately.
        "Now", GTK_RESPONSE_YES,
        // A start at a time the crew enter on the autostart screen.
        "Later", RESPONSE_AUTO_START,
        // On the next minute, as an early departure: distance zeroes at the
        // press, the clock at the minute.
        nextMinuteLabel.c_str(), RESPONSE_AUTO_START_NEXT_MINUTE,
        // Capitals: the one button here that ends something rather than
        // starting it.
        "ABORT STAGE", RESPONSE_ABORT_STAGE,
        "Back", GTK_RESPONSE_NO,
        nullptr);

    // Always shown, so the row never changes shape; it only acts when there
    // is a stage to abort (see stageAbortable). Otherwise it just closes the
    // dialog.
    const bool stage_to_abort = stageAbortable(*data->state);

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
    // Five buttons at the dialog font only fit the 1280 px co-pilot panel at
    // their natural widths; the button box's default sizes every one to the
    // widest and pushed the dialog onto the driver's panel.
    G_GNUC_BEGIN_IGNORE_DEPRECATIONS
    GtkWidget* action_area = gtk_dialog_get_action_area(GTK_DIALOG(dialog));
    G_GNUC_END_IGNORE_DEPRECATIONS
    if (action_area && GTK_IS_BUTTON_BOX(action_area)) {
        GList* buttons = gtk_container_get_children(GTK_CONTAINER(action_area));
        for (GList* b = buttons; b; b = b->next) {
            gtk_button_box_set_child_non_homogeneous(GTK_BUTTON_BOX(action_area),
                                                     GTK_WIDGET(b->data), TRUE);
        }
        g_list_free(buttons);
    }

    gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response == GTK_RESPONSE_YES) {
        performStageGo(data);
    } else if (response == RESPONSE_ABORT_STAGE) {
        if (stage_to_abort) {
            // Say what will actually go: the stage, the autostart, or both.
            const bool on_gauge = data->state->segment_current_number >= 0;
            const bool armed = data->state->auto_start_rally_time_s != 0;
            std::string text = on_gauge ? "Abort the stage?\n" : "Cancel the autostart?\n";
            if (on_gauge && armed) text += "\nThe autostart will be cancelled";
            text += "\nTotal and Trip keep their readings";
            if (confirmAbortStage(widget, text)) performAbortStage(data);
        }
    } else if (response == RESPONSE_AUTO_START) {
        on_show_autostart(widget, user_data);
    } else if (response == RESPONSE_AUTO_START_NEXT_MINUTE) {
        // Committed here -- no Set/Save step needed, so just return to
        // whatever screen was showing rather than detouring through the
        // autostart setup screen. Pressed at or after the printed minute, it
        // arms nothing -- arming the next one would start at a time the
        // button never showed, and the printed one has gone -- and says so,
        // or the navigator would sit at the line waiting for a start that is
        // not coming.
        if (autoStartTargetReachable(at_minute_ms, getRallyTime_ms(*data->state))) {
            setAutoStartToMinute(data, at_minute_ms);
        } else {
            flashMessage(widget, formatHm(at_minute_ms) + " has passed - reopen Stage Go");
        }
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
        applyDriverZero(data, false);
    } else if (response == RESPONSE_RESET_ZERO) {
        applyDriverZero(data, true);
    }
}

void applyDriverZero(AppData* data, bool reset) {
    if (reset) {
        data->state->ahead_behind_zero_offset_ms = 0;
    } else {
        // Zero the needle at this instant: the offset absorbs the current error.
        data->state->ahead_behind_zero_offset_ms +=
            static_cast<long>(data->aheadBehindSeconds * -1000.0);
    }
    ConfigFile::save(*data->state);
}

// "next", at any point in the segment (the old 500 m window is gone): the
// speed changes here, and what this segment gives up is handed to the next
// one -- so the next known point stays where the roadbook puts it, measured
// from the stage start.
void on_next_press(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    if (!nextAvailable(*data->state)) return;
    // Before the retime, so prev can put it back.
    recordSegmentChange(*data->state, data->state->segment_current_number, false);

    auto current_poll = data->poller->getMostRecent();
    // Corrected, like every other comparison against a roadbook distance: a
    // nav error has often just been knocked off with -10, and this figure is
    // written into the roadbook as the segment's real length.
    int64_t seg_count_diff = segmentCountsCorrected(data, current_poll);

    // The snapshot, not the saved roadbook: this retimes the stage actually
    // being driven, not the roadbook the next stage starts from.
    retimeSegmentBoundaryForward(data->state->stage_segments,
                                 data->state->segment_current_number,
                                 seg_count_diff, data->state->calibration);

    data->state->segment_current_number++;
    data->state->segment_start_cntr1 = current_poll.cntr1;
    data->state->segment_start_cntr2 = current_poll.cntr2;
    data->state->segment_start_adjust_cm = data->state->total_distance_adjust_cm;
    data->state->segment_start_time_ms = getRallyTime_ms(*data->state);
    rebaseTripToSegment(*data->state, current_poll.cntr1, current_poll.cntr2);

    ConfigFile::save(*data->state);
    notifyWebState(data);
}

// "prev": undo the last speed change -- a "next" pressed by mistake, or an
// auto-advance before the real change point. See undoSegmentChange.
void on_prev_press(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    if (!prevAvailable(*data->state)) return;

    auto poll = data->poller->getMostRecent();
    undoSegmentChange(*data->state, poll.cntr1, poll.cntr2, stageCountsCorrected(data, poll));

    ConfigFile::save(*data->state);
    notifyWebState(data);
}

// The distance the stage and the current segment have actually covered, with
// the crew's manual correction applied. Everything judged against a roadbook
// distance goes through these, so a wrong turn knocked off with the -10 button
// moves the ahead/behind figure, the segment boundary and the end of the stage
// together -- and not just the odometer, which is all the raw counts show.
int64_t stageCountsCorrected(AppData* data, const CounterPoll& poll) {
    int64_t raw = calculateDistanceCounts(*data->state, poll.cntr1, poll.cntr2,
        data->state->total_start_cntr1, data->state->total_start_cntr2);
    // The whole correction belongs to this stage: the stage's own start zeroed
    // it along with the counters.
    return correctedDistanceCounts(raw, data->state->total_distance_adjust_cm,
                                   data->state->calibration);
}

int64_t segmentCountsCorrected(AppData* data, const CounterPoll& poll) {
    int64_t raw = calculateDistanceCounts(*data->state, poll.cntr1, poll.cntr2,
        data->state->segment_start_cntr1, data->state->segment_start_cntr2);
    // Only the correction made since this segment began: an earlier one
    // already moved the boundary it preceded.
    return correctedDistanceCounts(raw,
        data->state->total_distance_adjust_cm - data->state->segment_start_adjust_cm,
        data->state->calibration);
}

gboolean update_display(gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    
    // Poll counters (respects 5ms minimum interval)
    bool polled = data->poller->poll(data->counter1, data->counter2, data->register_addr);

    // Sensor log: one row per actual counter read -- the raw counts and the
    // Total and Trip as the displays show them (RB-DEV-14, from upstream ae9753c).
    if (polled && data->debugLog && data->debugLog->isRunning()) {
        auto p = data->poller->getMostRecent();
        auto raw = data->poller->getMostRecentRaw();
        int64_t total_counts = calculateDistanceCounts(*data->state, p.cntr1, p.cntr2,
            data->state->total_start_cntr1, data->state->total_start_cntr2);
        int64_t trip_counts = calculateDistanceCounts(*data->state, p.cntr1, p.cntr2,
            data->state->trip_start_cntr1, data->state->trip_start_cntr2);
        data->debugLog->record(p.time_ms, static_cast<uint32_t>(raw.cntr1), static_cast<uint32_t>(raw.cntr2),
            countsToCentimeters(total_counts, data->state->calibration) / 100,
            countsToCentimeters(trip_counts, data->state->calibration) / 100);
    }

    // The stage's own distance is what ends it. Once it is driven out the
    // roadbook stops being frozen, so the next edit or recall shows up
    // straight away -- the ahead/behind figure at the line is left alone
    // until the crew actually change something.
    if (!data->state->stage_complete) {
        auto poll = data->poller->getMostRecent();
        int64_t stage_counts = stageCountsCorrected(data, poll);
        if (stageDistanceComplete(data->state->stage_segments, stage_counts)) {
            data->state->stage_complete = true;
            ConfigFile::save(*data->state);
            // The phone decides from the state message whether its Reset
            // Total asks about a running stage, so it must hear this end too.
            notifyWebState(data);
        }
    }

    // Stage log (RB-DEV-15): from the moment a stage starts until it ends or is
    // aborted, one row a second -- Total distance as displayed, the driver
    // display's speed, and the ahead/behind figure. Switching it on while a
    // stage is running starts a file at once.
    if (data->stageLog) {
        const int64_t sys_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const bool want = data->state->stage_logging && !data->state->stage_complete;
        if (want) {
            if (!data->stageLog->isRunning()) {
                if (!data->stageLog->start(appDirectory() + "/logs", sys_ms,
                                           data->state->last_memory_slot)) {
                    // Unwritable or full: switch the log off rather than retry
                    // every 10 ms; the app carries on.
                    data->state->stage_logging = false;
                    fprintf(stderr, "Stage logging switched off: cannot write the log\n");
                }
                data->stageLogLastSampleMs = 0;
            }
            if (data->stageLog->isRunning() && sys_ms - data->stageLogLastSampleMs >= 1000) {
                data->stageLogLastSampleMs = sys_ms;
                auto p = data->poller->getMostRecent();
                int64_t total_counts = calculateDistanceCounts(*data->state, p.cntr1, p.cntr2,
                    data->state->total_start_cntr1, data->state->total_start_cntr2);
                long total_m = adjustedDistanceMeters(
                    countsToCentimeters(total_counts, data->state->calibration),
                    data->state->total_distance_adjust_cm);
                data->stageLog->record(sys_ms, total_m,
                    data->smoothedSpeed < 0 ? 0.0 : data->smoothedSpeed,
                    data->aheadBehindSeconds);
            }
        } else if (data->stageLog->isRunning()) {
            data->stageLog->stop();   // stage ended, aborted or the switch went off
        }
    }

    // Check for auto-advance segments
    if (data->state->segment_current_number >= 0 && 
        data->state->segment_current_number < static_cast<long>(data->state->stage_segments.size())) {
        Segment& seg = data->state->stage_segments[data->state->segment_current_number];
        if (seg.autoNext) {
            auto current_poll = data->poller->getMostRecent();
            int64_t seg_count_diff = segmentCountsCorrected(data, current_poll);
            
            if (seg_count_diff >= seg.distance_counts) {
                // Advance to next segment
                if (data->state->segment_current_number < static_cast<long>(data->state->stage_segments.size()) - 1) {
                    // So prev can undo an automatic change too.
                    recordSegmentChange(*data->state, data->state->segment_current_number, true);
                    data->state->segment_current_number++;
                    auto current_poll = data->poller->getMostRecent();
                    data->state->segment_start_cntr1 = current_poll.cntr1;
                    data->state->segment_start_cntr2 = current_poll.cntr2;
                    data->state->segment_start_adjust_cm = data->state->total_distance_adjust_cm;
                    data->state->segment_start_time_ms = getRallyTime_ms(*data->state);
                    rebaseTripToSegment(*data->state, current_poll.cntr1, current_poll.cntr2);
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

// Both keypad entry points build the one shared Nero2.0 keypad.
GtkWidget* createNumericKeypad(AppData* data) { return createNeroKeypad(data); }
GtkWidget* createDateTimeKeypad(AppData* data) { return createNeroKeypad(data); }

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
    // A value typed on the Segments page is saved when the page is left.
    segmentsCommitActive(data);
    if (data->beepWaypointCommitTimer) {
        g_source_remove(data->beepWaypointCommitTimer);
        data->beepWaypointCommitTimer = 0;
        commitBeepWaypoints(data);
    }
    data->activeEntry = nullptr;
    data->activeBuffer = nullptr;
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

    char buf[160];
    snprintf(buf, sizeof(buf), "%.1f", countsToMeters(cntr_a, data->state->calibration));
    gtk_label_set_text(data->totalDistCalLabel, buf);

    char counts[200];
    snprintf(counts, sizeof(counts),
             "Sensor Count: 1: <span foreground=\"#FFFFFF\">%lld</span>"
             " 2: <span foreground=\"#FFFFFF\">%lld</span>"
             " Avg: <span foreground=\"#FFFFFF\">%lld</span>",
             static_cast<long long>(cntr1_diff), static_cast<long long>(cntr2_diff),
             static_cast<long long>(cntr_a));
    gtk_label_set_markup(data->totalCountCalLabel, counts);

    char current[120];
    snprintf(current, sizeof(current),
             "Pulses/KM: <span foreground=\"#FFFFFF\">%ld</span> Reset to",
             displayPulsesPerKm(data->state->typed_pulses_per_km, data->state->calibration));
    gtk_label_set_markup(data->calibrationCurrentLabel, current);

    // RB-CAL-06: Prop RPM, counter 1 alone over the poller's ~2 s span --
    // the same span the app's own speed uses. get10th() is all zeros until
    // there is enough history, which reads as no figure rather than a
    // nonsense one.
    CounterPoll tenth = data->poller->get10th();
    CounterPoll raw_now = data->poller->getMostRecentRaw();  // RPM is raw, not the reversing distance count
    int64_t elapsed_ms = tenth.time_ms ? raw_now.time_ms - tenth.time_ms : 0;
    std::string rpm = propRpmReading(propRpmFromCounts(raw_now.cntr1, tenth.cntr1,
                                                       elapsed_ms, data->state->prop_pulses_per_rev));
    if (data->propRpmLabel && rpm != data->propRpmShown) {
        data->propRpmShown = rpm;
        std::string markup = "<span foreground=\"#FFFFFF\">" + rpm + "</span>";
        gtk_label_set_markup(data->propRpmLabel, markup.c_str());
    }
}

// Committed as typed, like the other calibration-screen entries. An empty or
// out-of-range entry -- usually one caught mid-edit -- is simply not taken,
// so the readout keeps the last good figure rather than dividing by zero.
void on_prop_pulses_changed(GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    long ppr = std::strtol(gtk_entry_get_text(GTK_ENTRY(widget)), nullptr, 10);
    if (!validPropPulsesPerRev(ppr) || ppr == data->state->prop_pulses_per_rev) return;
    data->state->prop_pulses_per_rev = static_cast<int>(ppr);
    ConfigFile::save(*data->state);
}

// The phone-access address, for the Settings page's Wi-Fi line.
void updateWebLinkDisplay(AppData* data) {
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

void recallMemorySlot(AppData* data, int slot_1_based) {
    int slot = slot_1_based - 1;
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
                data->state->typed_pulses_per_km = 0.0;   // a measured figure now
                
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
    data->state->typed_pulses_per_km = pulses_per_km;   // shown back as typed

    // Same re-derivation as the measured-calibration path above.
    resyncSimCounterRate(data);

    // Recalculate count-based values in all segments from stable human
    // values, same as every other calibration change.
    recalculateSegmentCounts(*data->state);

    ConfigFile::save(*data->state);
    updateCalibrationDisplay(data);
}

void updateSensorModeLabel(AppData* data) {
    // The sensor in use is the white key and the other is dark; the keys say only
    // which sensor they are. The selection silently changes what every reading on the
    // page means, so it is always one or the other.
    const bool both = data->state->counters;
    auto lit = [](GtkWidget* k, bool on, const char* text) {
        if (!k) return;
        gtk_button_set_label(GTK_BUTTON(k), text);
        gtk_label_set_justify(GTK_LABEL(gtk_bin_get_child(GTK_BIN(k))), GTK_JUSTIFY_CENTER);
        GtkStyleContext* c = gtk_widget_get_style_context(k);
        if (on) gtk_style_context_add_class(c, "key-lit");
        else gtk_style_context_remove_class(c, "key-lit");
    };
    lit(data->sensor1Btn, !both, "sensor 1");
    lit(data->sensorBothBtn, both, "sensor 1&2");
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

void on_show_menu(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    releaseKeypadTarget(data);  // flush any typed value before the page changes
    gtk_stack_set_visible_child_name(data->copilotStack, "menu");
}

// A menu key: runs the same handler the page's old nav button did, so each
// page's own setup code still runs. A page that is not built yet is ignored.
void on_menu_choice(GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    const nero::MenuItem* item = static_cast<const nero::MenuItem*>(
        g_object_get_data(G_OBJECT(widget), "menu-item"));
    if (!item) return;
    if (!item->page) { on_exit_app(widget, data); return; }
    if (!gtk_stack_get_child_by_name(data->copilotStack, item->page)) return;

    const std::string page = item->page;
    if (page == "twinmaster")       on_show_twinmaster(widget, data);
    else if (page == "stagesetup")  on_show_segments(widget, data);
    else if (page == "datetime")    on_show_datetime(widget, data);
    else if (page == "calibration") on_show_calibration(widget, data);
    else gtk_stack_set_visible_child_name(data->copilotStack, item->page);
}

std::string appDirectory() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n <= 0) return ".";
    buf[n] = '\0';
    std::string path(buf);
    size_t slash = path.rfind('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

// Start a new sensor log in logs/ beside the executable, or flush and close
// the current one.
void setDebugLogging(AppData* data, bool on) {
    if (!data->debugLog) return;
    if (on) {
        auto now = std::chrono::system_clock::now();
        int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        data->debugLog->start(appDirectory() + "/logs", now_ms);
    } else {
        data->debugLog->stop();
    }
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

// --- Rally-clock trim (+0.1 / -0.1) --------------------------------------
// Separate from on_save_datetime() on purpose. That button applies the typed
// date/time at the instant it is pressed, so whatever fraction of a second
// the operator was late by is baked into the offset for the rest of the
// rally. The trim is the fine adjustment that corrects it, and it neither
// reads nor needs the entry boxes.
//
// Hold-to-repeat: the first press applies one tenth immediately, and holding
// past TRIM_REPEAT_DELAY_MS repeats every TRIM_REPEAT_INTERVAL_MS, so a whole
// second is about a second and a half of holding.
static const guint TRIM_REPEAT_DELAY_MS = 500;
static const guint TRIM_REPEAT_INTERVAL_MS = 150;

// Saves on every step rather than once on release: a released signal can be
// missed if the pointer grab breaks, and the config write is a few hundred
// bytes -- losing a trim silently would be the worse failure.
void applyRallyTrim(AppData* data, int steps) {
    data->state->rallyTimeOffset_ms = static_cast<long>(
        trimRallyOffsetMs(data->state->rallyTimeOffset_ms, steps));
    ConfigFile::save(*data->state);
    updateDateTimeDisplay(data);
}

static gboolean trim_repeat_tick(gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    applyRallyTrim(data, data->trimRepeatSteps);
    return G_SOURCE_CONTINUE;
}

static gboolean trim_repeat_start(gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    applyRallyTrim(data, data->trimRepeatSteps);
    // Hand over from the one-shot delay timer to the faster repeat timer.
    // The field must be reassigned before returning G_SOURCE_REMOVE, or the
    // release handler would try to cancel a source that is already gone.
    data->trimRepeatTimer = g_timeout_add(TRIM_REPEAT_INTERVAL_MS,
                                          trim_repeat_tick, data);
    return G_SOURCE_REMOVE;
}

void on_trim_rally_pressed(GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    int steps = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "trim-steps"));
    if (steps == 0) return;
    if (data->trimRepeatTimer) g_source_remove(data->trimRepeatTimer);
    data->trimRepeatSteps = steps;
    applyRallyTrim(data, steps);
    data->trimRepeatTimer = g_timeout_add(TRIM_REPEAT_DELAY_MS,
                                          trim_repeat_start, data);
}

void on_trim_rally_released(G_GNUC_UNUSED GtkWidget* widget, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    if (data->trimRepeatTimer) {
        g_source_remove(data->trimRepeatTimer);
        data->trimRepeatTimer = 0;
    }
    data->trimRepeatSteps = 0;
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
    // There is one autostart in the state, so this button reaches an early
    // departure armed from the Stage Go dialog as well as one entered here.
    // Only the early departure changed the stage when it was armed -- it
    // zeroed the distance and took the roadbook, while leaving the clock on
    // the previous stage's zero -- so only it has anything to undo.
    const bool cancelling_early_departure =
        data->state->auto_start_rally_time_s != 0 &&
        data->state->auto_start_early_departure;

    applyAutoStartArming(data, autoStartDisarmed());

    // The previous stage's distance zero is already gone, so there is nothing
    // to restore it to: the honest state is that no stage is running. Without
    // this the hold releases with the distance at the line and the clock still
    // on the old stage, and the gauge reads tens of minutes behind with the
    // needle pegged and the tone sounding.
    if (cancelling_early_departure) {
        endStageAsIdle(data);
        ConfigFile::save(*data->state);
        notifyWebState(data);
    }
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
