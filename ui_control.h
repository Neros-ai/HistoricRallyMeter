#ifndef UI_CONTROL_H
#define UI_CONTROL_H

#include <gtk/gtk.h>
#include "rally_types.h"

// Creates the "Driving Controls (SIM)" window: START/STOP and 25-50 km/h
// (5 km/h step) speed buttons that drive AppData::simCounter1/2 in real
// time. Only meaningful when RALLY_SIM_I2C=1 (simCounter1/2 non-null).
GtkWidget* createControlWindow(AppData* data);

// Refreshes the status label and highlights the active speed button.
// Safe to call even if createControlWindow() was never called
// (controlStatusLabel is nullptr, so it's a no-op).
void updateControlDisplay(AppData* data);

// Flashes beepFlashLabel for ~1.5s naming which mode fired -- the sandbox
// has no audio device, so this is the only visible confirmation that a
// navigation or timing beep actually triggered. Safe to call even if
// createControlWindow() was never called (beepFlashLabel is nullptr).
void flashBeepWarning(AppData* data, bool navigation_fired);

// Sets the sim speed and unpauses -- the panel's speed buttons.
void applySpeedToSimCounters(AppData* data, double kph);

// Re-derives the sim counters' rate from the CURRENT calibration, preserving
// paused state. Call after any calibration change; a no-op when no speed has
// been set or when not simulating.
void resyncSimCounterRate(AppData* data);

#endif // UI_CONTROL_H
