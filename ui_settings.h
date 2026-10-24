#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#include <gtk/gtk.h>
#include "rally_types.h"

// The Nero2.0 Settings page: Beep Assist and tone on the left, system options
// on the right. Everything that used to sit on Segments and Date/Time.
GtkWidget* createSettingsScreen(AppData* data);

// Per tick while visible: fps / CPU temperature and the Wi-Fi address.
void updateSettingsDisplay(AppData* data);

#endif // UI_SETTINGS_H
