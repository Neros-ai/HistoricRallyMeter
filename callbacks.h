#ifndef CALLBACKS_H
#define CALLBACKS_H

#include <gtk/gtk.h>
#include "rally_types.h"
#include "calculations.h"   // AutoStartArming

// Column widths for the stage-setup segment table, in pixels. Declared once
// and used by both the header row in createStageSetupScreen() and the data
// rows in refreshSegmentList(): the two were previously laid out by different
// mechanisms with separately hand-tuned paddings, so they lined up only by
// coincidence and drifted apart whenever a value's width changed.
constexpr int SEGMENT_COL_SPEED    = 140;
constexpr int SEGMENT_COL_DISTANCE = 170;
constexpr int SEGMENT_COL_AUTO     = 60;
constexpr int SEGMENT_COL_TIME     = 70;
constexpr int SEGMENT_COL_DELETE   = 70;
constexpr int SEGMENT_COL_SPACING  = 15;

gboolean on_window_delete(GtkWidget* widget, GdkEvent* event, gpointer user_data);
void on_unit_toggle(GtkWidget* widget, gpointer user_data);
void on_total_reset(GtkWidget* widget, gpointer user_data);
void on_trip_reset(GtkWidget* widget, gpointer user_data);
void on_stage_go(GtkWidget* widget, gpointer user_data);
void on_next_segment(GtkWidget* widget, gpointer user_data);
void on_next_prev_segment(GtkWidget* widget, gpointer user_data);
void on_show_segments(GtkWidget* widget, gpointer user_data);
void on_show_calibration(GtkWidget* widget, gpointer user_data);
void on_show_twinmaster(GtkWidget* widget, gpointer user_data);
void on_show_datetime(GtkWidget* widget, gpointer user_data);
void on_add_segment(GtkWidget* widget, gpointer user_data);
void on_delete_segment(GtkWidget* widget, gpointer user_data);
void on_calibration_start(GtkWidget* widget, gpointer user_data);
void on_save_calibration(GtkWidget* widget, gpointer user_data);
// Reset calibration to an operator-entered pulses/km value, replacing the
// stock "reset to 1m per pulse" with one that works for any device.
void on_reset_calibration_pulses(GtkWidget* widget, gpointer user_data);
void on_save_datetime(GtkWidget* widget, gpointer user_data);
gboolean on_force_single_display_toggle(GtkSwitch* sw, gboolean state, gpointer user_data);
gboolean on_tone_enabled_toggle(GtkSwitch* sw, gboolean state, gpointer user_data);
void on_tone_type_toggled(GtkWidget* widget, gpointer user_data);
void refreshSegmentList(AppData* data);
void updateCalibrationDisplay(AppData* data);
// Refresh the calibration screen's sensor-selection label.
void updateSensorModeLabel(AppData* data);
void updateDateTimeDisplay(AppData* data);
gboolean update_display(gpointer user_data);
void on_keypad_digit(GtkWidget* widget, gpointer user_data);
void on_keypad_clear(GtkWidget* widget, gpointer user_data);
void on_keypad_backspace(GtkWidget* widget, gpointer user_data);
gboolean on_entry_focus(GtkWidget* widget, GdkEvent* event, gpointer user_data);
// Focus handler for the Beep Assist waypoint view, mirroring on_entry_focus.
gboolean on_textview_focus(GtkWidget* widget, GdkEvent* event, gpointer user_data);
void on_segment_entry_changed(GtkWidget* widget, gpointer user_data);
void on_segment_auto_toggled(GtkWidget* widget, gpointer user_data);
void on_memory_set(GtkWidget* widget, gpointer user_data);
void on_memory_recall(GtkWidget* widget, gpointer user_data);
void on_memory_clear(GtkWidget* widget, gpointer user_data);

// How long the waypoint list must sit unedited before it is parsed and
// saved. Long enough that a multi-digit number is never committed
// half-typed, short enough that leaving the screen straight after typing
// still catches it (releaseKeypadTarget flushes it anyway).
constexpr unsigned BEEP_WAYPOINT_COMMIT_MS = 800;

// Beep Assist controls on the stage-setup screen.
void on_beep_waypoints_changed(GtkTextBuffer* buffer, gpointer user_data);
// Parse and persist the waypoint list now, cancelling nothing. Called by the
// debounce timeout and by releaseKeypadTarget when leaving the screen.
void commitBeepWaypoints(AppData* data);
// Rewrites the waypoint box from state (after a memory recall).
void refreshBeepWaypointView(AppData* data);
// Clear the keypad's target and flush any pending waypoint edit. Called on
// every screen switch -- the keypad target is shared process-wide.
void releaseKeypadTarget(AppData* data);
gboolean on_beep_enable_toggled(GtkWidget* widget, gboolean state, gpointer user_data);
void on_beep_mode_toggled(GtkWidget* widget, gpointer user_data);
void on_beep_advance_changed(GtkWidget* widget, gpointer user_data);
void on_alarm_set(GtkWidget* widget, gpointer user_data);
void on_alarm_clear(GtkWidget* widget, gpointer user_data);
void on_adj_driver_zero(GtkWidget* widget, gpointer user_data);
void on_set_sensor_1(GtkWidget* widget, gpointer user_data);
void on_set_sensor_both(GtkWidget* widget, gpointer user_data);
void on_exit_app(GtkWidget* widget, gpointer user_data);
void on_show_autostart(GtkWidget* widget, gpointer user_data);
void on_autostart_set(GtkWidget* widget, gpointer user_data);
void on_autostart_clear(GtkWidget* widget, gpointer user_data);
void updateAutoStartDisplay(AppData* data);
void performStageGo(AppData* data);

// Replaces the armed autostart wholesale (see AutoStartArming). Every arming,
// re-arming and clearing path goes through here, so a press always overrules
// whatever was pending rather than merging with it.
void applyAutoStartArming(AppData* data, const AutoStartArming& arming);
// Starts a stage from an autostart trigger, honouring whether it was armed as
// an early departure (clock only) or an ordinary autostart (clock and
// distance together).
void performAutoStart(AppData* data);
// The two halves of a stage start, separated so an early departure can zero
// distance when the operator presses the button and the clock at the
// appointed minute.
void zeroDistanceBaselines(AppData* data);
void zeroTimeBaselines(AppData* data);

// Adopts the edited roadbook as the stage roadbook, but only when no stage
// is under way. Call after any change to state->segments.
void adoptRoadbookIfIdle(AppData* data);

// Milliseconds until an armed autostart fires (0 when nothing is armed;
// negative once the moment has passed), and whether that autostart is
// currently holding the stage readouts at zero. Both derived from state, so
// the driver display, the phone telemetry and anything else agree.
int64_t autoStartRemaining_ms(const AppData* data);
bool autoStartHoldActive(const AppData* data);
int64_t getAutoStartEpochMs();
GtkWidget* createNumericKeypad(AppData* data);
GtkWidget* createDateTimeKeypad(AppData* data);

// Manual distance correction, buttoned on the co-pilot main screen's Total
// row. on_distance_adjust writes BOTH total_distance_adjust_cm and
// trip_distance_adjust_cm -- Total and Trip are two windows onto the same
// wheel-count measurement, so a wheel-slip correction applies to both. It
// reads a signed "delta_m" object datum carrying the step in metres.
// on_distance_set only ever resolves total_distance_adjust_cm -- pinning to
// an exact roadbook figure has no Trip equivalent.
void on_distance_adjust(GtkWidget* widget, gpointer user_data);
void on_distance_set(GtkWidget* widget, gpointer user_data);

#endif // CALLBACKS_H
