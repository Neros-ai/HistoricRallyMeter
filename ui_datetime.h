#ifndef UI_DATETIME_H
#define UI_DATETIME_H

#include <gtk/gtk.h>
#include "rally_types.h"
#include "ui_segments.h"

// The Nero2.0 Date/Time page: rally, computer and phone time to a tenth, the
// rally clock's offset, and the keys that set one from another.
GtkWidget* createDateTimeScreen(AppData* data);

// Per tick while the page is visible.
void updateDateTimeDisplay(AppData* data);

#endif // UI_DATETIME_H
