#ifndef RALLY_TYPES_H
#define RALLY_TYPES_H

#include <string>
#include <cstdint>
#include <vector>

#ifndef RALLY_NO_GTK
#include <gtk/gtk.h>
#endif

// Forward declarations
class I2CCounter;
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
    I2CCounter* counter1;
    I2CCounter* counter2;
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
    GtkButton* unitToggleBtn;
    
    // Rally gauge
    GtkWidget* rallyGaugeDrawingArea;
    double aheadBehindSeconds = 0.0;
    double smoothedSpeed = -1.0;      // EMA-filtered current speed for display
    int gaugeScale = 1;               // 0=±3s(green), 1=±10s(yellow), 2=±5min(red)
    int64_t gaugeScaleChangeTime = 0; // Timestamp of last scale change (2s cooldown)
    double segmentProgress = 0.0;     // fraction (0..1) of the current segment driven
    bool inSegment = false;           // true while within the current segment's distance
    
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
    GtkWidget* memoryRecallBtns[5] = {};  // Recall buttons for memory slots
    
    // Calibration screen
    GtkWidget* calibrationScreen;
    GtkWidget* calibrationMainBox;  // Main horizontal container for keypad
    GtkLabel* totalDistCalLabel;
    GtkLabel* totalCountCalLabel;
    GtkEntry* rallyDistEntry;
    GtkWidget* calibrationKeypad;   // Numeric keypad for calibration
    GtkLabel* sensorModeLabel;      // "Currently set to sensor 1 / both sensors"
    
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
    // Read-only stage panel on the main navigator screen, and the text it is
    // currently showing -- compared each tick so the label is only touched
    // when something actually changed, rather than 100 times a second.
    GtkLabel* stageStatusLabel = nullptr;
    std::string stageStatusShown;
    // "Early Departure: ENABLED/DISABLED", under the countdown box.
    GtkLabel* earlyDepartureLabel = nullptr;
    bool autoStartTriggered = false;
    
    // Tone generator for speed adjustment alerts
    ToneGenerator* toneGen = nullptr;
    
    int updateCount = 0;
    int64_t lastUpdateCountTime_ms = 0;
};
#endif // RALLY_NO_GTK

#endif // RALLY_TYPES_H
