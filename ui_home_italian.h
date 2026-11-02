#ifndef UI_HOME_ITALIAN_H
#define UI_HOME_ITALIAN_H

#include "rally_types.h"

// The italian sector card on Home (RB-NAV-44): the pc being driven with its
// count-down line and the next line, or the navi / pm panel with the metres to go,
// and the co's arrival. Drawn in a w x h area that fills Home's columns 3 and 4.
// part 1: the table (w x h); part 2: the foot strip (error / co2 arrival), left of the menu key.
GtkWidget* createItalianCard(AppData* data, int w, int h, int part);

// Redraw it from the current state.
void updateItalianCard(GtkWidget* card);

#endif
