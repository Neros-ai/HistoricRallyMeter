#ifndef UI_DATETIME_H
#define UI_DATETIME_H

#include <gtk/gtk.h>
#include <string>
#include "rally_types.h"
#include "ui_segments.h"

// The Nero2.0 Date/Time page: rally, computer and phone time to a tenth, the
// rally clock's offset, and the keys that set one from another.
GtkWidget* createDateTimeScreen(AppData* data);

// Used by the phone as well as the keys: the computer clock to the phone's
// (false with `error` set when it cannot), whether a phone clock has reported
// in the last few seconds, and the rally clock to a time of day today.
bool setComputerClockToPhone(AppData* data, std::string* error);
bool phoneClockKnown(AppData* data);
void setRallyToEntered(AppData* data, long tod_s);

// Per tick while the page is visible.
void updateDateTimeDisplay(AppData* data);

#endif // UI_DATETIME_H
