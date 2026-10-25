#ifndef UI_HOME_H
#define UI_HOME_H

#include <gtk/gtk.h>
#include <string>
#include "rally_types.h"

// The Nero2.0 co-pilot Home page (two-display mode). Geometry and the sums
// behind it are in tools/layout-preview/nero2/home.html; the fixed inner panel
// is 1268 x 388.
//
// Figures and tables are drawn with cairo into fixed-size areas rather than
// laid out as labels: a 40 or 88 px label's natural height is taller than the
// row it has to sit in, and an oversized page would push the whole window past
// 400 px.
struct HomeUi {
    // What the drawing areas show. Refreshed by updateHomeDisplay().
    std::string figure[3];      // Total, Trip, next
    std::string elapsed[2];     // Total, Trip (mm:ss)
    std::string speedLine;      // "50<>60 kph", or empty
    std::string history[6];     // Trip history, newest first
    std::string stageNo = "-";
    bool autoArmed = false;
    std::string autoTime;       // hh:mm:ss, or empty
    bool autoTimePast = false;  // the stage start time has already gone by and no stage is armed or running
    struct Row { std::string kph, at; bool now = false; };
    Row rows[3];
    bool arrivalValid = false;
    int arrivalKph = 0;
    int arrivalMin = 0;

    GtkWidget* figureArea[3] = {};
    GtkWidget* elapsedArea[3] = {};   // Total, Trip, next (speed line)
    GtkWidget* historyArea = nullptr;
    GtkWidget* stageArea = nullptr;
    GtkWidget* stack = nullptr;       // "main" | "picker"
    GtkWidget* stageNoBtn = nullptr;
    GtkWidget* pickerBtns[5] = {};
    GtkWidget* freezeBtn = nullptr;
    GtkWidget* reverseBtn = nullptr;
    GtkWidget* minuteCaption = nullptr;   // text carrier: hh:mm under the next-minute flag
    GtkWidget* goArea = nullptr;          // the stage go / stage abort key's drawing
    bool goWasAbort = false;
    GtkWidget* flagArea = nullptr;        // the flag key's drawing, redrawn when the minute turns
    GtkWidget* schedArea = nullptr;       // the scheduled-start key's drawing
    std::string schedState;               // caption + armed mark last drawn
    guint whoopTimer = 0;
};

GtkWidget* createHomeScreen(AppData* data);

// Reverse on or off, from the box's key or the phone: the poller counts
// backwards, the key lights and the whoop repeats while it is on.
void setReverse(AppData* data, bool on);

// True while a stage is live: no new stage can be loaded then.
bool stageChangeLocked(const RallyState& st);

// Freeze on or off, from the box's key or a phone; both follow the one state.
void toggleHomeFreeze(AppData* data);

// Refresh the drawn figures from the AppData readouts. Does nothing while
// frozen. Call after the TwinMaster labels have been updated.
void updateHomeDisplay(AppData* data);

#endif // UI_HOME_H
