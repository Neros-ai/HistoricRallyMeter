#ifndef UI_SEGMENTS_H
#define UI_SEGMENTS_H

#include <gtk/gtk.h>
#include <string>
#include "rally_types.h"

// The Nero2.0 Segments page (menu > segments). The table, add-distance box,
// stage start fields and clear keys sit on stage_rows.h; edits autosave into
// the memory on show -- there is no Set, Recall or Save key.
struct SegUi;

GtkWidget* createStageSetupScreen(AppData* data);

// Reload the page from the state: pick the memory being edited, fill the
// start fields and rebuild the table. Called whenever the page is opened.
void refreshSegmentList(AppData* data);

// Apply the value typed into the keypad's current entry, if it belongs to this
// page. Called when another field is tapped and when the page is left.
void segmentsCommitActive(AppData* data);

// Per-tick: the clock and the Autostart tick.
void updateSegmentsDisplay(AppData* data);

#endif // UI_SEGMENTS_H
