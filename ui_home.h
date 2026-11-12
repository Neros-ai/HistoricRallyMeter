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
    std::string speedLine;      // mm:ss to the next speed change, or empty
    std::string history[4];     // Trip history, newest four, newest first
    std::string stageNo = "-";
    bool autoArmed = false;
    bool unitsMph = false;      // speed-table header reads MPH
    int distanceKm = 0;         // the distance unit (DIST_M / DIST_KM / DIST_MI): stage table "at km" / "at m" / "at mi" follows Settings
    std::string autoTime;       // hh:mm:ss, or empty
    bool autoTimePast = false;  // the stage start time has already gone by and no stage is armed or running
    struct Row { std::string kph, at; bool now = false; };
    Row rows[3];
    bool arrivalValid = false;
    int arrivalKph = 0;
    int arrivalMin = 0;
    // Surrey (RB-NAV-43): navi in the third row, the countdown panel in the stage column while a
    // stage runs, and the last time taken under Trip history.
    bool surrey = false;
    bool italian = false;             // navi total in cyan, as surrey
    bool surreyRunning = false;       // countdown panel instead of Stage arrival
    std::string cdMetres, cdTime;     // countdown panel figures
    std::string timeTakenLabel, timeTaken;   // "time taken ss1", "9:20.6"; empty = none

    GtkWidget* figureArea[3] = {};
    GtkWidget* elapsedArea[3] = {};   // Total, Trip, next (speed line)
    GtkWidget* historyArea = nullptr;
    GtkWidget* stageArea = nullptr;
    GtkWidget* stack = nullptr;       // "main" | "italian"
    GtkWidget* stageNoBtn = nullptr;
    // Italian (RB-NAV-44): the sector card page in the stack, its own clock and stage keys,
    // the left block's keys (relabelled) and the right column keys that italian blanks.
    GtkWidget* italianCard = nullptr;
    GtkWidget* italianFoot = nullptr;    // the strip under the table, left of the menu key
    GtkWidget* italianClock = nullptr;
    GtkWidget* stageNoBtn2 = nullptr;
    GtkWidget* leftKeys[3] = {};
    GtkWidget* flagKey = nullptr;
    GtkWidget* schedKey = nullptr;
    bool stackSettled = false;           // the stack has been put on its main page once it was on screen
    GtkWidget* adjKeys[2] = {nullptr, nullptr};   // the + and - distance keys: "+10" / "-10", or "+.01" / "-.01" in miles
    int adjKeyUnit = -1;                 // distance unit their labels were last set for
    int labelMode = -1;                  // rally type the labels were last set for
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
