#ifndef UI_CALIBRATION_H
#define UI_CALIBRATION_H

#include <gtk/gtk.h>
#include "rally_types.h"

// The Nero2.0 Calibration page: content as before, keys in a right-hand
// column level with the lines they act on, keypad far right.
GtkWidget* createCalibrationScreen(AppData* data);

#endif // UI_CALIBRATION_H
