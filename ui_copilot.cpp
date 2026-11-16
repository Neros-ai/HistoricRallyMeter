#include "stage_phase.h"
#include "ui_copilot.h"
#include <cstring>
#include "ui_driver.h"
#include "ui_control.h"
#include "calculations.h"
#include "sector.h"
#include "rally_types.h"
#include "rally_state.h"
#include "counter_poller.h"
#include "callbacks.h"
#include "surrey.h"
#include "config_file.h"
#include "webserver/qr_display.h"
#include "calculations.h"
#include "tone_generator.h"
#include "nero_frame.h"
#include "ui_home.h"
#include "home_calc.h"
#include "ui_segments.h"
#include "stage_rows.h"
#include "ui_datetime.h"
#include "ui_calibration.h"
#include "ui_settings.h"
#include "ui_setup.h"
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
        "button { background-color: #333333; color: #FFFFFF; font-weight: bold; font-family: monospace; border: 2px solid #FFFFFF; }"
        "entry { background-color: #222222; color: #FFFFFF; font-weight: bold; }"
        ".title-label { font-size: 22px; }"
        ".info-label { font-size: 20px; }"
        ".clock-label { font-size: 30px; font-family: monospace; }"
        // Date/Time screen: line the clock readouts up with the entry boxes
        // underneath them. A GtkEntry indents its text by its own padding plus
        // its 1px border, so plain labels above it start ~9px to the left and
        // the three rows look ragged. Pin the entry's padding here rather than
        // inherit the theme's, then pad the labels to match it exactly.
        ".dt-clock-entry { padding-left: 8px; padding-right: 8px; }"
        ".dt-clock-value { padding-left: 9px; }"
        ".dist-heading { font-size: 28px; font-weight: bold; font-family: monospace; }"
        ".dist-value { font-size: 64px; font-weight: bold; font-family: monospace; }"
        ".dist-unit { font-size: 20px; font-weight: bold; font-family: monospace; }"
        ".time-label { font-size: 22px; font-family: monospace; color: #CCCCCC; }"
        // Monospace: the panel is a fixed-width table of speeds and
        // distances, and a proportional font makes its columns ragged.
        ".stage-status { font-size: 22px; font-family: monospace; }"
        ".counter-fault { background: #C00000; color: #FFFFFF; font-size: 44px; font-weight: bold; padding: 12px 40px; }"
        ".reset-button { font-size: 36px; }"
        ".nav-button { font-size: 20px; }"
        ".segment-label { font-size: 18px; }"
        // The Navigation/Timing lead-in values are read at a glance while
        // moving, so they get their own larger size rather than inheriting
        // the 18px used for captions.
        ".beep-advance-entry { font-size: 24px; }"
        ".segment-row entry, .segment-row button, .segment-row checkbutton { font-size: 18px; }"
        ".new-segment-row label, .new-segment-row entry, .new-segment-row button, .new-segment-row checkbutton { font-size: 18px; }"
        ".segment-list-frame { border: 1px solid #444444; background-color: #111111; }"
        "scrollbar slider { min-width: 20px; min-height: 20px; }"
        "scrollbar.vertical slider { min-width: 20px; }"
        "scrollbar trough { min-width: 24px; }"
        "button.memory-populated { background-image: none; background-color: #FFFFFF; color: #000000; }"
        ".adjust-button { font-size: 28px; font-weight: bold; font-family: monospace; }"
        // Countdown, drawn a second time over the embedded gauge in
        // single-display mode -- smaller than the driver display's, which is
        // sized for a whole 800x480 panel.
        ".countdown-box-embedded { border: 3px solid white; background-color: #000000; }"
        ".countdown-label-embedded { font-size: 40px; font-family: monospace; color: white;"
        " font-weight: bold; }"
        ".early-departure-label-embedded { font-size: 18px; color: white; font-weight: bold; }"
        ".instruction-label { font-size: 16px; color: #CCCCCC; }"
        ".compact-entry { min-height: 0; padding-top: 0; padding-bottom: 0; }"
        ".tone-mode-row label, .tone-mode-row checkbutton { font-size: 20px; }"
        // Nero2.0 menu page: 40 px key name over a 20 px grey caption.
        // Keys that must stay inside a narrow column ("A-B, B-C", +10).
        "button.key-small { font-size: 22px; }"
        // Settings, Distance units: five keys on one row, so no side padding and a slightly smaller face.
        "button.key-tight { font-size: 20px; padding: 0; }"
        "button.seg-clr { font-size: 16px; font-weight: bold; padding: 0; }"
        "button.key-clr { padding-left: 2px; padding-right: 2px; }"
        "button.key-24 { font-size: 24px; }"
        "button.key-21 { font-size: 21px; padding: 0; }"
        "button.key-17 { font-size: 17px; padding: 0; }"
        "button.key-18 { font-size: 18px; padding: 0; }"
        ".seg-head { color: #00FFFF; font-size: 18px; }"
        ".seg-clock { font-size: 30px; font-family: monospace; }"
        ".seg-small { font-size: 18px; }"
        ".seg-row label, .seg-row entry { font-size: 21px; }"
        ".seg-dim { color: #AAAAAA; }"
        ".seg-current { border: 2px solid #00FFFF; }"
        "button.seg-tag { font-size: 18px; font-weight: bold; padding: 0; }"
        "button.seg-tag-navi { background-image: none; background-color: #666666; color: #FFFFFF; }"
        "button.seg-tag-pc { background-image: none; background-color: #D01010; color: #FFFFFF; }"
        "button.seg-tag-ct { background-image: none; background-color: #1E50C8; color: #FFFFFF; }"
        "button.seg-tag-pm { background-image: none; background-color: #F0D000; color: #000000; }"
        "button.seg-tag-co { background-image: none; background-color: #10A030; color: #FFFFFF; }"
        ".seg-entry { padding-left: 4px; padding-right: 4px; }"
        ".seg-del { font-size: 13px; font-family: monospace; font-weight: bold; padding: 0; }"
        ".seg-startentry { font-size: 26px; border-color: transparent; }"
        ".seg-addentry { font-size: 24px; }"
        ".dt-label { color: #00FFFF; font-size: 24px; }"
        // The same bold monospace, at the same 56 px, as the rally and computer
        // times above it (drawn with cairo on this page).
        ".dt-entry { font-family: monospace; font-weight: bold; font-size: 56px;"
        " padding-top: 0; padding-bottom: 0; min-height: 0; padding-left: 0; color: #00FFFF; }"
        ".cal-row { color: #00FFFF; font-size: 28px; font-family: monospace; }"
        ".cal-fig { color: #FFFFFF; }"
        ".cal-entry { font-size: 28px; font-family: monospace; color: #FFFFFF; }"
        "button.key-tiny { font-size: 15px; padding: 0; }"
        ".set-name { font-size: 22px; font-family: monospace; }"
        ".setup-label { font-size: 20px; font-family: monospace; }"
        ".set-url { font-size: 22px; font-family: monospace; color: #00FFFF; }"
        ".set-note { font-size: 17px; font-family: monospace; color: #AAAAAA; }"
        ".set-entry { font-size: 22px; }"
        ".home-clock { font-size: 42px; font-family: monospace; }"
        // An engaged key (freeze, reverse).
        "button.key-lit { background-image: none; background-color: #FFFFFF; }"
        "button.key-lit label { color: #000000; }"
        ".menu-item { font-size: 40px; }"
        ".menu-caption { font-size: 20px; color: #CCCCCC; }",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    // An unavailable heading button -- prev with nothing to undo, next on
    // the last segment -- has to look it. The box's anti-dimming gtk.css
    // keeps every unfocused widget at full brightness, and with no window
    // manager the windows are always unfocused; it sits at USER priority,
    // above this application's, so the dimming is set one step above that.
    GtkCssProvider* disabled = gtk_css_provider_new();
    gtk_css_provider_load_from_data(disabled,
        "button.dist-heading:disabled, button.dist-heading:disabled:backdrop"
        " { color: #555555; border-color: #555555; }"
        "button.dist-heading:disabled label, button.dist-heading:disabled label:backdrop"
        " { color: #555555; }",
        -1, NULL);
    gtk_style_context_add_provider_for_screen(
        gdk_screen_get_default(),
        GTK_STYLE_PROVIDER(disabled),
        GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
    g_object_unref(disabled);
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

// Navigation mode's gap between the two beeps of its "bing bong", start to
// start. Shared: the box's second beep is fired off a GTK timer below, and the
// phone is sent this so it can schedule its own pair.
static constexpr int BEEP_ASSIST_NAV_GAP_MS = 150;

// Plays and reports one beep -- shared by the independent navigation and
// timing checks in tickCopilot() so a tick where both are due
// fires both, back to back, rather than one silently eating the other's
// beep for the same waypoint.
// Louder than the button-click beep, which keeps ToneGenerator's 0.20
// default. Raising that default instead would have made every button click
// on the box louder too, since the global click hook shares playBeep().
static constexpr double BEEP_ASSIST_AMP = 0.45;

// pc countdown beeps: the single tick at 4..1. At 5 s the
// navigation double beep (same pitch, length and gap). On 0 a shorter beep,
// so its start marks the instant to be on the tube and it is not the 4..1 tick.
static constexpr double PC_BEEP_FREQ_HZ = 3200.0;
static constexpr int PC_BEEP_TICK_MS = 150;
static constexpr int PC_BEEP_ZERO_MS = 60;

static void fireBeepAssist(AppData* data, double waypoint_m, double travelled_m, bool navigation) {
    // Navigation mode plays its tone twice in quick succession ("bing
    // bong"), Timing mode plays it once.
    // Logged only under RALLY_DEBUG: this is the display hot path, and an
    // unconditional line per beep grows app.log all rally long. The on-screen
    // flash below is the always-available confirmation that the waypoint
    // logic fired, including in the sandbox.
    static const bool debug_beeps = (getenv("RALLY_DEBUG") != nullptr);
    if (debug_beeps) {
        std::cerr << "Beep Assist fired: waypoint " << waypoint_m
                  << "m, travelled " << travelled_m << "m"
                  << (navigation ? " (navigation, double beep)" : " (timing, single beep)")
                  << std::endl;
    }
    flashBeepWarning(data, navigation);

    const double freq_hz = navigation ? BEEP_ASSIST_NAV_FREQ_HZ : BEEP_ASSIST_TIMING_FREQ_HZ;
    const ToneWaveform wave = navigation ? BEEP_ASSIST_NAV_WAVE : BEEP_ASSIST_TIMING_WAVE;
    const int duration_ms = navigation ? BEEP_ASSIST_NAV_DURATION_MS
                                       : BEEP_ASSIST_TIMING_DURATION_MS;

    // Publish it for the phone whether or not this box has a speaker: the
    // sandbox has no ALSA device and toneGen is null there, and the phone is
    // exactly how you hear Beep Assist in that case.
    data->lastBeep.seq++;
    data->lastBeep.freq_hz = freq_hz;
    data->lastBeep.duration_ms = duration_ms;
    data->lastBeep.amplitude = BEEP_ASSIST_AMP;
    data->lastBeep.triangle = (wave == ToneWaveform::Triangle);
    data->lastBeep.twice = navigation;
    data->lastBeep.gap_ms = navigation ? BEEP_ASSIST_NAV_GAP_MS : 0;

    if (data->toneGen) {
        data->toneGen->playBeep(freq_hz, wave, duration_ms, BEEP_ASSIST_AMP);
        if (navigation) {
            // g_timeout_add, not a blocking sleep -- this runs on the GTK
            // main thread and must not stall the UI for the gap.
            g_timeout_add(BEEP_ASSIST_NAV_GAP_MS, [](gpointer d) -> gboolean {
                static_cast<AppData*>(d)->toneGen->playBeep(
                    BEEP_ASSIST_NAV_FREQ_HZ, BEEP_ASSIST_NAV_WAVE,
                    BEEP_ASSIST_NAV_DURATION_MS, BEEP_ASSIST_AMP);
                return G_SOURCE_REMOVE;
            }, data);
        }
    }
}

static gboolean on_counter_fault_tap(G_GNUC_UNUSED GtkWidget* w, G_GNUC_UNUSED GdkEventButton* e, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    data->poller->clearCounterFault();
    updateCounterFaultWidgets(data);
    notifyWebStateNow(data);
    return TRUE;
}

GtkWidget* makeCounterFaultWidget(AppData* data) {
    GtkWidget* box = gtk_event_box_new();
    GtkWidget* label = gtk_label_new("counter fault");
    gtk_style_context_add_class(gtk_widget_get_style_context(label), "counter-fault");
    gtk_container_add(GTK_CONTAINER(box), label);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    g_signal_connect(box, "button-press-event", G_CALLBACK(on_counter_fault_tap), data);
    gtk_widget_show_all(box);
    gtk_widget_set_no_show_all(box, TRUE);
    gtk_widget_hide(box);
    return box;
}

void updateCounterFaultWidgets(AppData* data) {
    const bool fault = data->poller && data->poller->counterFault();
    if (data->faultCopilot) gtk_widget_set_visible(data->faultCopilot, fault);
    if (data->faultDriver) gtk_widget_set_visible(data->faultDriver, fault);
}

// A press-warning mark (italian): the navigation double beep at 5 s, a single beep at 4, 3, 2
// and 1, and a shorter beep on 0. sound: the box's speaker. announce: the phone is told (it
// sounds the beep on receipt).
static void playMark(AppData* data, PcBeep beep, bool sound, bool announce) {
    const bool five = beep == PcBeep::Five;
    const int duration_ms = five ? BEEP_ASSIST_NAV_DURATION_MS
        : beep == PcBeep::Zero ? PC_BEEP_ZERO_MS : PC_BEEP_TICK_MS;
    const double freq_hz = five ? BEEP_ASSIST_NAV_FREQ_HZ : PC_BEEP_FREQ_HZ;
    if (announce) {
        data->lastBeep.seq++;          // the phone sounds it too
        data->lastBeep.freq_hz = freq_hz;
        data->lastBeep.duration_ms = duration_ms;
        data->lastBeep.amplitude = BEEP_ASSIST_AMP;
        data->lastBeep.triangle = false;
        data->lastBeep.twice = five;
        data->lastBeep.gap_ms = five ? BEEP_ASSIST_NAV_GAP_MS : 0;
    }
    if (!sound || !data->toneGen) return;
    const ToneWaveform wave = five ? BEEP_ASSIST_NAV_WAVE : ToneWaveform::Sine;
    data->toneGen->playBeep(freq_hz, wave, duration_ms, BEEP_ASSIST_AMP);
    if (five) {
        // Same second beep as navigation mode. A timer, not a sleep:
        // this runs on the GTK thread.
        g_timeout_add(BEEP_ASSIST_NAV_GAP_MS, [](gpointer d) -> gboolean {
            static_cast<AppData*>(d)->toneGen->playBeep(
                BEEP_ASSIST_NAV_FREQ_HZ, BEEP_ASSIST_NAV_WAVE,
                BEEP_ASSIST_NAV_DURATION_MS, BEEP_ASSIST_AMP);
            return G_SOURCE_REMOVE;
        }, data);
    }
}

// Sounds the beeps the tick decided (data->tick.beeps, rally_tick.h), in the order it decided them.
void playTickBeeps(AppData* data) {
    for (const TickBeep& b : data->tick.beeps) {
        if (b.kind == TickBeep::Mark) playMark(data, static_cast<PcBeep>(b.mark), b.sound, b.announce);
        else fireBeepAssist(data, b.waypoint_m, b.travelled_m, b.kind == TickBeep::AssistNavigation);
    }
}

// Draws the co-pilot window. The beeps are decided by tickCopilot (rally_tick.h) and sounded by
// playTickBeeps just before this, at the rally time this draws with (tick.copilotNowMs).
void updateCopilotDisplay(AppData* data) {
    updateCounterFaultWidgets(data);
    auto current_poll = data->poller->getMostRecent();
    const int64_t current_time_ms = data->tick.copilotNowMs;
    
    // Rally clock
    std::string rally_time = formatTime(current_time_ms);
    setLabelText(data->copilotRallyClockLabel, rally_time.c_str());

    // Re-assert the window's fixed size every tick: GTK grows a window to
    // fit its widest-ever content but never shrinks it back down, so a
    // single transient spike in a distance value (e.g. a big Next-segment
    // reading, however briefly shown) would otherwise leave the co-pilot
    // window oversized -- overlapping the driver panel -- for the rest of
    // the session, even once the values shrink back to normal.
    if (!data->singleDisplayMode) {
        gtk_window_resize(GTK_WINDOW(data->copilotWindow), 1280, 400);
    }

    // Total distance, whichever screen is visible
    int64_t total_count_diff = calculateDistanceCounts(*data->state,
        current_poll.cntr1, current_poll.cntr2,
        data->state->total_start_cntr1, data->state->total_start_cntr2);

    // Check which screen is visible
    GtkWidget* visible_child = gtk_stack_get_visible_child(data->copilotStack);
    
    // Each other page keeps its own live display. It no longer ends the update here: the Navigation figures below are
    // the text the phone shows, so they must keep running whatever page the box is on (item 26: the phone's Total and
    // Trip froze while the box sat on Calibration, and showed the old unit's figure after a unit change).
    if (visible_child == data->calibrationScreen) updateCalibrationDisplay(data);
    else if (visible_child == data->dateTimeScreen) updateDateTimeDisplay(data);   // live clock
    else if (visible_child == data->settingsScreen) updateSettingsDisplay(data);
    else if (visible_child == data->stageSetupScreen) updateSegmentsDisplay(data);   // Nero2.0 Segments: clock and the Autostart tick
    else if (visible_child == data->autoStartScreen) updateAutoStartDisplay(data);

    // Total distance
    long total_m = adjustedDistanceMetersMm(
        countsToMm(total_count_diff, data->state->calibration),
        data->state->total_distance_adjust_mm);
    int64_t total_duration_ms = current_time_ms - data->state->total_start_time_ms;
    int64_t total_secs = elapsedForDisplay(total_duration_ms / 1000,
                                           autoStartHoldActive(data));
    
    std::stringstream ss;
    const int dist_km = data->state->distance_unit;
    const char* total_unit = distUnitName(dist_km);
    std::string total_str = formatDistance(static_cast<double>(total_m), dist_km);
    setLabelText(data->totalDistLabel, total_str.c_str());
    setLabelText(data->totalUnitLabel, total_unit);

    setLabelText(data->totalTimeLabel, formatElapsedInterval(total_secs).c_str());
    
    // Trip distance
    int64_t trip_count_diff = calculateDistanceCounts(*data->state,
        current_poll.cntr1, current_poll.cntr2,
        data->state->trip_start_cntr1, data->state->trip_start_cntr2);
    long trip_m = adjustedDistanceMetersMm(
        countsToMm(trip_count_diff, data->state->calibration),
        data->state->trip_distance_adjust_mm);
    int64_t trip_duration_ms = current_time_ms - data->state->trip_start_time_ms;
    int64_t trip_secs = elapsedForDisplay(trip_duration_ms / 1000,
                                          autoStartHoldActive(data));
    
    const char* trip_unit = distUnitName(dist_km);
    std::string trip_str = formatDistance(static_cast<double>(trip_m), dist_km);
    setLabelText(data->tripDistLabel, trip_str.c_str());
    setLabelText(data->tripUnitLabel, trip_unit);

    setLabelText(data->tripTimeLabel, formatElapsedInterval(trip_secs).c_str());
    
    // Read-only stage panel. Built from `segments` -- the roadbook as set,
    // which is what "what is loaded" means -- not from the running stage's
    // frozen snapshot.
    if (data->stageStatusLabel) {
        std::string panel = formatStageStatusTable(
            data->state->segments, data->state->units,
            formatAutoStartStatus(data->state->auto_start_rally_time_s,
                                  data->state->auto_start_early_departure,
                                  getAutoStartEpochMs()),
            STAGE_STATUS_MAX_ROWS);
        if (panel != data->stageStatusShown) {
            // Markup, not plain text: the captions are coloured (see
            // formatStageStatusTable).
            gtk_label_set_markup(data->stageStatusLabel, panel.c_str());
            data->stageStatusShown = panel;
        }
    }

    // The next / prev keys name what they step by: speed (greek / surrey) or
    // substage (italian). Shared by the home page and the single-display layout.
    {
        GtkWidget* keys[2] = { data->nextPrevBtn, data->prevBtn };
        for (int k = 0; k < 2; k++) {
            if (!keys[k]) continue;
            const char* want = readoutKeyName(data->state->rally_type, 3 + k);
            const char* have = gtk_button_get_label(GTK_BUTTON(keys[k]));
            if (!have || strcmp(have, want) != 0) {
                gtk_button_set_label(GTK_BUTTON(keys[k]), want);
                GtkWidget* lbl = gtk_bin_get_child(GTK_BIN(keys[k]));
                if (GTK_IS_LABEL(lbl)) gtk_label_set_justify(GTK_LABEL(lbl), GTK_JUSTIFY_CENTER);
            }
        }
    }

    // Next segment info: distance remaining in current segment + speed of next segment
    if (data->state->segment_current_number >= 0 &&
        data->state->segment_current_number < static_cast<long>(data->state->stage_segments.size())) {
        // The countdown runs to the next speed change, on through rows that
        // carry the speed on (Nero2.0 ditto rows); italian counts each row.
        const bool greek_like = !isItalian(*data->state);
        long remaining_m;
        if (greek_like) {   // row end from the stage start minus Total as shown: the two always sum to the roadbook figure
            remaining_m = countdownMetres(data->state->stage_segments, data->state->segment_current_number,
                                          totalShownMetres(*data->state, current_poll.cntr1, current_poll.cntr2));
        } else {            // italian: metres left in the row, from the sector start position
            remaining_m = rowCountdownMetres(data->state->stage_segments, data->state->segment_current_number,
                                             stagePositionMm(*data->state, current_poll.cntr1, current_poll.cntr2));
        }
        const char* next_unit = distUnitName(dist_km);
        std::string next_str = formatDistance(static_cast<double>(remaining_m), dist_km);
        setLabelText(data->nextDistLabel, next_str.c_str());
        setLabelText(data->nextUnitLabel, next_unit);
        
        long next_seg_idx = greek_like
            ? nextChangeIndex(data->state->stage_segments, data->state->segment_current_number)
            : data->state->segment_current_number + 1;
        bool has_next = next_seg_idx >= 0 && next_seg_idx < static_cast<long>(data->state->stage_segments.size());
        double next_kph = has_next ? data->state->stage_segments[next_seg_idx].target_speed_kph : 0.0;
        double next_display_speed = next_kph;
        // Same conversion updateDriverDisplay() already applies to
        // targetSpeedLabel -- showing "40 mph" for a 40 km/h segment would be
        // wrong by a safety-relevant margin, not just a labelling nicety.
        if (data->state->units) {
            next_display_speed = kphToMph(next_display_speed);
        }
        setLabelText(data->nextSpeedLabel,
            segmentSpeedTransition(next_display_speed, has_next, data->state->units).c_str());

        // Live at any point in the segment: next while one follows, prev
        // while one precedes.
        gtk_widget_set_sensitive(data->nextPrevBtn, nextKeyEnabled(*data->state));
        gtk_widget_set_sensitive(data->prevBtn, prevKeyEnabled(*data->state, current_poll.cntr1, current_poll.cntr2));
    } else if (data->state->stage_segments.empty()) {
        setLabelText(data->nextDistLabel, "---.---");
        setLabelText(data->nextUnitLabel, "m");
        setLabelText(data->nextSpeedLabel, "---");
        gtk_widget_set_sensitive(data->nextPrevBtn, FALSE);
        gtk_widget_set_sensitive(data->prevBtn, FALSE);
    } else {
        setLabelText(data->nextDistLabel, "---.---");
        setLabelText(data->nextUnitLabel, "m");
        setLabelText(data->nextSpeedLabel, segmentSpeedTransition(0.0, false, false).c_str());
        gtk_widget_set_sensitive(data->nextPrevBtn, FALSE);
        gtk_widget_set_sensitive(data->prevBtn, FALSE);
    }

    // Nero2.0 Home: redraw the figures and stage panel from the labels above.
    updateHomeDisplay(data);
}

// Create TwinMaster screen - two-column layout for 1280x400
// The driver's compact gauge for single-display mode, with the autostart
// countdown laid over it. Sets copilotGaugeArea and the three countdown
// pointers; the caller packs the returned overlay.
GtkWidget* createEmbeddedGauge(AppData* data) {
    // The gauge goes inside an overlay so the autostart countdown can be
    // laid over it. The driver window owns a countdown of its own, but
    // that window is never shown in single-display mode, so the crew saw
    // no countdown at all. The overlay covers the gauge panel only,
    // leaving the navigator their controls while it runs.
    GtkWidget* gaugeOverlay = gtk_overlay_new();
    gtk_widget_set_hexpand(gaugeOverlay, TRUE);
    gtk_widget_set_vexpand(gaugeOverlay, TRUE);

    data->copilotGaugeArea = gtk_drawing_area_new();
    gtk_widget_set_hexpand(data->copilotGaugeArea, TRUE);
    gtk_widget_set_vexpand(data->copilotGaugeArea, TRUE);
    g_signal_connect(data->copilotGaugeArea, "draw", G_CALLBACK(on_gauge_draw), data);
    gtk_container_add(GTK_CONTAINER(gaugeOverlay), data->copilotGaugeArea);

    // Rebuilt here rather than reparented: the driver window's widgets are
    // already packed into that window's own overlay. Its copies stay where
    // they are, unseen. updateDriverDisplay() drives whichever widgets
    // these three pointers name, so it needs no change -- but this must run
    // after createDriverWindow(), which sets them first. main.cpp creates
    // the driver window before the co-pilot one.
    data->countdownLabel = GTK_LABEL(gtk_label_new("T- 00:00:00"));
    gtk_widget_set_margin_start(GTK_WIDGET(data->countdownLabel), 20);
    gtk_widget_set_margin_end(GTK_WIDGET(data->countdownLabel), 20);
    gtk_widget_set_margin_top(GTK_WIDGET(data->countdownLabel), 10);
    gtk_widget_set_margin_bottom(GTK_WIDGET(data->countdownLabel), 10);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(GTK_WIDGET(data->countdownLabel)),
        "countdown-label-embedded");

    GtkWidget* countdownBox = gtk_event_box_new();
    gtk_container_add(GTK_CONTAINER(countdownBox), GTK_WIDGET(data->countdownLabel));
    gtk_widget_set_halign(countdownBox, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(countdownBox, GTK_ALIGN_CENTER);
    gtk_style_context_add_class(gtk_widget_get_style_context(countdownBox),
                                "countdown-box-embedded");

    data->earlyDepartureLabel = GTK_LABEL(gtk_label_new(""));
    gtk_label_set_justify(data->earlyDepartureLabel, GTK_JUSTIFY_CENTER);
    gtk_label_set_xalign(data->earlyDepartureLabel, 0.5);
    gtk_style_context_add_class(
        gtk_widget_get_style_context(GTK_WIDGET(data->earlyDepartureLabel)),
        "early-departure-label-embedded");

    GtkWidget* countdownStack = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_halign(countdownStack, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(countdownStack, GTK_ALIGN_CENTER);
    gtk_box_pack_start(GTK_BOX(countdownStack), countdownBox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(countdownStack),
                       GTK_WIDGET(data->earlyDepartureLabel), FALSE, FALSE, 0);
    data->countdownContainer = countdownStack;

    gtk_overlay_add_overlay(GTK_OVERLAY(gaugeOverlay), countdownStack);
    gtk_widget_show_all(countdownStack);
    gtk_widget_hide(countdownStack);


    return gaugeOverlay;
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
    
    for (GtkWidget* btn : {clearBtn, setBtn, backBtn})
        gtk_style_context_add_class(gtk_widget_get_style_context(btn), "nav-button");
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
    {   // the stack sits under an overlay so the counter-fault warning can float over every screen
        GtkWidget* faultOverlay = gtk_overlay_new();
        gtk_container_add(GTK_CONTAINER(faultOverlay), GTK_WIDGET(data->copilotStack));
        data->faultCopilot = makeCounterFaultWidget(data);
        gtk_overlay_add_overlay(GTK_OVERLAY(faultOverlay), data->faultCopilot);
        gtk_container_add(GTK_CONTAINER(window), faultOverlay);
    }
    
    // Create all screens
    // Two displays get the Nero2.0 Home page; the single-display box keeps the
    // embedded-gauge layout until the driver-display plan reworks it.
    data->twinMasterScreen = createHomeScreen(data);   // both modes: single-display embeds the gauge in it
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
    
    data->settingsScreen = createSettingsScreen(data);
    gtk_stack_add_named(data->copilotStack, data->settingsScreen, "settings");
    data->setupScreen = createSetupScreen(data);
    gtk_stack_add_named(data->copilotStack, data->setupScreen, "setup");

    // The menu page last: it greys any key whose page is not in the stack.
    data->menuScreen = createMenuScreen(data);
    gtk_stack_add_named(data->copilotStack, data->menuScreen, "menu");

    // Show TwinMaster by default
    gtk_stack_set_visible_child_name(data->copilotStack, "twinmaster");
    
    return window;
}
