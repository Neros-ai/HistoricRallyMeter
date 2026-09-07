#ifndef RALLY_TYPES_H
#define RALLY_TYPES_H

#include <cstdint>
#include <vector>

#ifndef RALLY_NO_GTK
#include <gtk/gtk.h>
#endif

#include "simple_tone.h"

// Forward declarations
class ICounter;
class SimCounter;
class RallyState;
class CounterPoller;
class ToneGenerator;
class RallyWebServer;

// Segment structure (high precision)
struct Segment {
    double target_speed_kph = 0.0;               // user-entered speed in KPH (calibration-independent)
    double target_speed_counts_per_hour = 0.0;   // counts per hour (recalculated on calibration change)
    double distance_m = 0.0;                      // user-entered distance in meters (calibration-independent)
    double distance_counts = 0.0;                 // distance in counts (recalculated on calibration change)
    bool autoNext = true;
};

// Counter polling data with 10-second rolling average
struct CounterPoll {
    uint64_t cntr1;
    uint64_t cntr2;
    int64_t time_ms;
};

#ifndef RALLY_NO_GTK
// Application data structure (requires GTK)
struct AppData {
    ICounter* counter1;
    ICounter* counter2;
    uint8_t register_addr;
    RallyState* state;
    CounterPoller* poller;
    
    // Driver window
    GtkWidget* driverWindow;
    GtkLabel* currentSpeedLabel;
    GtkLabel* tripSpeedLabel;
    GtkLabel* totalSpeedLabel;
    GtkLabel* targetSpeedLabel;
    GtkLabel* gaugeTargetLabel;    // target speed shown below gauge
    GtkLabel* aheadBehindLabel;
    GtkLabel* speedAdjustArrowsLabel;
    GtkLabel* nextSegLabel;
    GtkLabel* updatesPerSecLabel;
    GtkLabel* cpuTempLabel;
    GtkLabel* unitsLabel;  // Shows KPH or MPH in header
    GtkLabel* driverTotalDistLabel;
    GtkLabel* driverTotalUnitLabel;
    GtkLabel* driverTripDistLabel;
    GtkLabel* driverTripUnitLabel;
    GtkButton* unitToggleBtn;
    
    // Rally gauge
    GtkWidget* rallyGaugeDrawingArea;
    double aheadBehindSeconds = 0.0;
    double smoothedSpeed = -1.0;      // EMA-filtered current speed for display
    double segmentProgress = 0.0;     // fraction (0..1) of the current segment driven
    bool inSegment = false;           // true while within the current segment's distance

    // Runtime latch for the alternative time-error-only tone
    // (simple_tone.h). Deliberately not persisted -- restarting the app
    // with a fresh latch is correct, matching aheadBehindSeconds above.
    SimpleToneState simpleToneState;

    // Compact (800x480-style) driver layout: values drawn inside the gauge
    GtkWidget* driverSpeedsBox = nullptr;  // left pane, hidden in compact mode
    bool driverCompactMode = false;

    // Single-display mode: one 1280x400 monitor only; driver window hidden,
    // compact driver display embedded in the TwinMaster right panel
    bool singleDisplayMode = false;
    GtkWidget* copilotGaugeArea = nullptr;
    
    // Co-pilot window
    GtkWidget* copilotWindow;
    GtkStack* copilotStack;  // Stack for multiple screens
    GtkLabel* copilotRallyClockLabel;
    
    // TwinMaster screen
    GtkWidget* twinMasterScreen;
    GtkLabel* totalDistLabel;
    GtkLabel* totalUnitLabel;
    GtkLabel* totalTimeLabel;
    GtkLabel* tripDistLabel;
    GtkLabel* tripUnitLabel;
    GtkLabel* tripTimeLabel;
    GtkLabel* segmentInfoLabel;
    GtkLabel* nextDistLabel;
    GtkLabel* nextUnitLabel;
    GtkLabel* nextSpeedLabel;
    GtkWidget* nextPrevBtn;
    GtkWidget* adjZeroBtn;
    GtkLabel* alarmCountdownLabel;
    GtkWidget* alarmClearBtn;
    int64_t alarmSoundStartTime = 0;    // when doorbell started (0 = not ringing)

    GtkLabel* webUrlLabel = nullptr;
    GtkWidget* webQrArea = nullptr;
    RallyWebServer* webServer = nullptr;
    
    // Stage setup screen
    GtkWidget* stageSetupScreen;
    GtkWidget* stageSetupMainBox;  // Main horizontal container
    GtkListBox* segmentListBox;
    GtkEntry* targetSpeedEntry;
    GtkEntry* distanceEntry;
    GtkCheckButton* autoNextCheck;
    GtkWidget* numericKeypad;      // Numeric keypad container
    GtkEntry* activeEntry;         // Currently focused entry for keypad input
    // Keypad target when the focused widget is a multi-line view rather than
    // a single-line entry (the Beep Assist waypoint list). At most one of
    // activeEntry / activeBuffer is ever non-null, so a keypress cannot land
    // in two places.
    GtkTextBuffer* activeBuffer = nullptr;
    GtkWidget* memoryRecallBtns[5] = {};  // Recall buttons for memory slots

    // Beep Assist runtime cursors: index of the next waypoint not yet beeped,
    // one per mode. Separate cursors so navigation and timing can each fire
    // independently for the same waypoint -- a shared cursor meant whichever
    // condition tripped first silently consumed the OTHER mode's beep for
    // that waypoint too (e.g. a navigation lead-in tripping before the
    // scheduled time meant the timing beep never fired at all).
    // Deliberately not persisted -- on restart they are re-derived from the
    // distance already travelled, so a power blip mid-stage does not replay
    // every waypoint the car has already passed.
    size_t beepNextNavIndex = 0;
    size_t beepNextTimingIndex = 0;
    // Set whenever the cursors no longer describe where the car is: at
    // startup, on an edit to the waypoint list, and when Beep Assist or one
    // of its modes is switched on. They are re-derived on the next display
    // tick rather than on the spot, because the counter has not necessarily
    // been polled yet at window-construction time -- deriving from an
    // unpolled counter yields a negative travelled distance, a cursor of 0,
    // and a replay of every waypoint already behind the car.
    bool beepCursorsStale = true;
    // Debounce for waypoint-list edits: the buffer's "changed" signal fires
    // per keystroke, and committing on each one both saved the whole config
    // to the SD card per character and could beep on a half-typed number.
    guint beepWaypointCommitTimer = 0;
    GtkTextBuffer* beepWaypointBuffer = nullptr;
    GtkEntry* beepAdvanceMetresEntry = nullptr;
    GtkEntry* beepAdvanceSecondsEntry = nullptr;

    // Calibration screen
    GtkWidget* calibrationScreen;
    GtkWidget* calibrationMainBox;  // Main horizontal container for keypad
    GtkLabel* totalDistCalLabel;
    GtkLabel* totalCountCalLabel;
    GtkEntry* rallyDistEntry;
    GtkWidget* calibrationKeypad;   // Numeric keypad for calibration
    GtkLabel* sensorModeLabel;      // "Currently set to sensor 1 / both sensors"
    GtkLabel* calibrationCurrentLabel;  // "Current Calibration: N pulses/KM"
    GtkEntry* resetPulsesEntry;  // operator-entered target for RB-CAL-03's reset
    GtkLabel* calibrationClockLabel;  // rally clock, shares the title row

    // Calibration baseline values (set when "start" is pressed)
    uint64_t cal_start_cntr1 = 0;
    uint64_t cal_start_cntr2 = 0;
    bool cal_started = false;  // True once "start" has been pressed
    
    // Date/Time setup screen
    GtkWidget* dateTimeScreen;
    GtkWidget* datetimeKeypad;
    GtkLabel* systemClockLabel;
    GtkLabel* systemTimeLabel;
    GtkLabel* rallyClockLabel;
    GtkLabel* rallyTimeLabel;
    GtkEntry* dateEntry;
    GtkEntry* timeEntry;
    
    // Auto Start setup screen
    GtkWidget* autoStartScreen;
    GtkLabel* autoStartRallyClockLabel;
    GtkLabel* autoStartTimeLabel;
    GtkEntry* autoStartTimeEntry;
    GtkWidget* autoStartKeypad;
    
    // Driver countdown overlay
    GtkWidget* countdownOverlay;
    GtkLabel* countdownLabel;
    bool autoStartTriggered = false;
    
    // Tone generator for speed adjustment alerts
    ToneGenerator* toneGen = nullptr;
    
    int updateCount = 0;
    int64_t lastUpdateCountTime_ms = 0;

    // Sim Control Panel (3rd display, dev/testing only — created only when
    // RALLY_SIM_I2C=1). Drives the simulated counters in real time.
    SimCounter* simCounter1 = nullptr;
    SimCounter* simCounter2 = nullptr;
    GtkWidget* controlWindow = nullptr;
    GtkWidget* controlSpeedButtons[6] = {};  // 25,30,35,40,45,50 km/h
    GtkWidget* controlStartBtn = nullptr;
    GtkWidget* controlStopBtn = nullptr;
    GtkLabel* controlStatusLabel = nullptr;
    double controlSpeedKph = 0.0;

    // Beep Assist has no audible feedback in the sandbox (no ALSA device),
    // so this label stands in for the sound: it flashes briefly whenever a
    // navigation or timing beep fires, naming which mode triggered it.
    GtkLabel* beepFlashLabel = nullptr;
};
#endif // RALLY_NO_GTK

#endif // RALLY_TYPES_H
