#ifndef UI_DRIVER_H
#define UI_DRIVER_H

#include <gtk/gtk.h>
#include "rally_types.h"

void updateDriverDisplay(AppData* data);   // draws data->tick (after tickDrive)
void updateDriverShown(AppData* data);     // the dial's texts, after the tick has acted on the autostart
// End of the tick: queue both gauge drawing areas for a redraw only when something on_gauge_draw
// reads has changed since the last one queued (gauge_redraw.h). GTK still draws on expose/resize.
void queueGaugeRedrawIfChanged(AppData* data);
GtkWidget* createDriverWindow(AppData* data);

// Rally gauge draw callback (also used for the gauge embedded in the
// TwinMaster right panel in single-display mode)
gboolean on_gauge_draw(GtkWidget* widget, cairo_t* cr, gpointer user_data);

#endif // UI_DRIVER_H
