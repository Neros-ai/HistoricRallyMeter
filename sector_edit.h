#ifndef SECTOR_EDIT_H
#define SECTOR_EDIT_H

// Italian editing on the Segments page (RB-NAV-44): the same guarded edit path as
// stage_edit.h -- refused while a stage is live, saved, adopted and sent to the phone.

#include <cstddef>
#include "rally_types.h"

// Tap on the type tag: navi -> pc -> ct -> pm -> co -> navi. The new type's ref is the
// next of its kind; a pc keeps the time the row already takes.
void sectorEditStepType(AppData* data, size_t row);
// The time card's number for a row (a label only).
void sectorEditSetRef(AppData* data, size_t row, int ref);
// A pc's card time, in whole seconds; its speed follows.
void sectorEditSetPcDuration(AppData* data, size_t row, double seconds);
// The sector's set duration (CO to CO), in seconds; -1 clears it.
void sectorEditSetSectorDuration(AppData* data, long seconds);

#endif
