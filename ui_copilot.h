#ifndef UI_COPILOT_H
#define UI_COPILOT_H

#include <gtk/gtk.h>
#include "rally_types.h"

void updateCopilotDisplay(AppData* data);
GtkWidget* createCopilotWindow(AppData* data);

// The embedded driver gauge (single-display mode) in an overlay with its countdown.
GtkWidget* createEmbeddedGauge(AppData* data);

#endif // UI_COPILOT_H
