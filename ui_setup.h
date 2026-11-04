#ifndef UI_SETUP_H
#define UI_SETUP_H

#include <gtk/gtk.h>
#include "rally_types.h"

// The Setup page, opened by the spanner key at the foot of the Settings keypad
// column: version, hostname, the connected displays (rotate, keyboard, reset
// layout), Bluetooth speaker and Wi-Fi. Ported from the upstream Setup screen.
GtkWidget* createSetupScreen(AppData* data);

// The spanner key for the Settings page.
GtkWidget* createSpannerKey(AppData* data);

// Reconnect the remembered Bluetooth speaker at startup when asked to.
void bluetoothAutoconnectOnStartup(AppData* data);

// The same Setup actions, from the phone. Wi-Fi list is filled only when asked.
void phoneResetLayout(AppData* data);
void phoneBtConnect(AppData* data);
void phoneBtDisconnect(AppData* data);
void phoneWifiList(AppData* data);
void phoneDisplaysList(AppData* data);
void phoneRotate(AppData* data, const std::string& output);
void phoneKeyboard(AppData* data, const std::string& output);
void phoneRememberSpeaker(AppData* data);
void phoneUpdate(AppData* data);
void phoneWifiUse(AppData* data, const std::string& name);

#endif // UI_SETUP_H
