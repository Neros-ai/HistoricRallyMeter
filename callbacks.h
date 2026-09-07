#ifndef CALLBACKS_H
#define CALLBACKS_H

#include <gtk/gtk.h>
#include "rally_types.h"
#include "calculations.h"   // AutoStartArming

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
void on_reset_calibration_1m(GtkWidget* widget, gpointer user_data);
void on_save_datetime(GtkWidget* widget, gpointer user_data);
gboolean on_force_single_display_toggle(GtkSwitch* sw, gboolean state, gpointer user_data);
void refreshSegmentList(AppData* data);
void updateCalibrationDisplay(AppData* data);
void updateDateTimeDisplay(AppData* data);
gboolean update_display(gpointer user_data);
void on_keypad_digit(GtkWidget* widget, gpointer user_data);
void on_keypad_clear(GtkWidget* widget, gpointer user_data);
void on_keypad_backspace(GtkWidget* widget, gpointer user_data);
gboolean on_entry_focus(GtkWidget* widget, GdkEvent* event, gpointer user_data);
void on_segment_entry_changed(GtkWidget* widget, gpointer user_data);
void on_segment_auto_toggled(GtkWidget* widget, gpointer user_data);
void on_memory_set(GtkWidget* widget, gpointer user_data);
void on_memory_recall(GtkWidget* widget, gpointer user_data);
void on_memory_clear(GtkWidget* widget, gpointer user_data);
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

// Zero the two halves of a stage start separately. An early departure zeroes
// the distance when it is armed and the clock at the appointed minute.
void zeroDistanceBaselines(AppData* data);
void zeroTimeBaselines(AppData* data);

// Fires a pending autostart, branching on which kind it is.
void performAutoStart(AppData* data);

// Replaces the armed autostart wholesale (see AutoStartArming). Every
// arming, re-arming and clearing path goes through here, so a press always
// overrules whatever was pending rather than merging with it.
void applyAutoStartArming(AppData* data, const AutoStartArming& arming);

// Milliseconds until an armed autostart fires (0 when nothing is armed,
// negative once the moment has passed), and whether that autostart is
// currently holding the stage readouts at zero. Both derived from state, so
// the driver display and the phone telemetry cannot disagree.
int64_t autoStartRemaining_ms(const AppData* data);
bool autoStartHoldActive(const AppData* data);
int64_t getAutoStartEpochMs();
GtkWidget* createNumericKeypad(AppData* data);
GtkWidget* createDateTimeKeypad(AppData* data);

#endif // CALLBACKS_H
