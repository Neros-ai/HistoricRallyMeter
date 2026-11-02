#ifndef STAGE_EDIT_H
#define STAGE_EDIT_H

// Editing the stage on show, at the level of AppData: the Segments page and the
// phone both call these, so a change made on one is made exactly the same way
// as on the other. Every function autosaves into the memory being edited (see
// stage_rows.h for the row model), re-derives the beep distances, adopts the
// roadbook if no stage is running, saves the config and tells the phone.
//
// No widgets are touched here; the Segments page redraws itself afterwards.

#include <string>
#include "rally_types.h"

// True while a stage is live (see stageLive): no stage may be edited or loaded then. Every setter below
// does nothing while this holds; the Segments page and the phone say so.
bool stageEditLocked(const RallyState& state);

// The memory being edited (1-5): the one on show, creating a home for a stage
// loaded from an older config in the first empty slot.
int stageEditSlot(AppData* data);

// Make memory slot_1 (1-5, empty ones included) the stage on show.
void stageEditSelect(AppData* data, int slot_1);

// "1,000;1,500": add a row at each distance, splitting a row a distance falls
// inside (RB-SEG-16). Returns how many rows were made; `rest` (optional) gets
// the text from the first unreadable distance on; `notice` (optional) names
// distances that were refused (already a point / not past the start).
int stageEditAddDistances(AppData* data, const std::string& text, std::string* rest,
                          std::string* notice = nullptr);

// The roadbook box of a row, typed on the Segments page (row 0 sets the stage's first box).
void stageEditSetBox(AppData* data, size_t row, long box);

void stageEditSetSpeed(AppData* data, size_t row, double kph);
void stageEditSetChange(AppData* data, size_t row, bool chg);
void stageEditSetAuto(AppData* data, size_t row, bool on);
void stageEditDeleteRow(AppData* data, size_t row);
// New end distance for a row (see setRowTo). False if it was refused.
bool stageEditSetRowTo(AppData* data, size_t row, double new_to_m);
void stageEditSetFirst(AppData* data, long number, double from_m);
// time_s < 0 clears the time.
void stageEditSetStart(AppData* data, long time_s, long distance_m, std::string* notice = nullptr);
void stageEditClearStage(AppData* data);
void stageEditClearAll(AppData* data);

// Tick: arm an autostart at the stage's start time today. False, with `error`
// filled, when there is no start time or it has passed. Unticking disarms.
bool stageEditSetAutostart(AppData* data, bool on, std::string* error);

// Save the edit (counts, beep distances, memory, config, phone). The setters
// above call this; exposed for the Segments page's own text entries.
void stageEditCommit(AppData* data);

#endif // STAGE_EDIT_H
