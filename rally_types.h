#ifndef RALLY_TYPES_H
#define RALLY_TYPES_H

#include <cstdint>
#include <vector>
#include <string>

#ifndef RALLY_NO_GTK
#include <gtk/gtk.h>
#endif

#include "simple_tone.h"
#include "arrow_tone.h"
#include "tone_cadence.h"
#include "driver_trial.h"
#include "rally_tick.h"
#include "gauge_redraw.h"

// Forward declarations
class ICounter;
class SimCounter;
class RallyState;
class CounterPoller;
class ToneGenerator;
class RallyWebServer;
class DebugLog;
class StageLog;
class ConfigSaver;
struct HomeUi;  // Nero2.0 Home page widgets and texts (ui_home.h)

// Segment structure (high precision)
struct Segment {
    double target_speed_kph = 0.0;               // user-entered speed in KPH (calibration-independent)
    double distance_m = 0.0;                      // user-entered distance in meters (calibration-independent)
    bool autoNext = true;
    // Nero2.0: false on a leg that carries the speed above it (the table shows
    // a ditto mark); true where the speed changes. Row 1 always counts as a change.
    bool changesSpeed = true;
    // Roadbook box number typed for this row; -1 = the row above's + 1 (row 1
    // takes the memory's first_number). Shown on the Segments page, never used
    // in a calculation.
    long box = -1;
    // Italian rally type (RB-NAV-44). SEG_LEG is every greek row; the others make a stage a sector.
    int type = 0;                 // SegmentType (sector.h)
    int ref = 0;                  // the time card's number (pc16, pm1, ct6, co2); a label only
    double pc_duration_s = 0.0;   // pc rows: the card's time, typed in whole seconds
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
    // Every config save while the app runs goes through this (config_saver.h, callbacks.h saveConfig*).
    ConfigSaver* saver = nullptr;
    
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
    // Text carriers for the compact driver layout, which draws these with
    // cairo rather than showing a widget. Plain strings: as GtkLabels they
    // were created, never parented, and never destroyed.
    std::string driverTotalDistText = "0";
    std::string driverTotalUnitText = "m";
    std::string driverTripDistText  = "0";
    std::string driverTripUnitText  = "m";
    // Zone currently shown by the gauge, for gaugeZoneHysteretic().
    int gaugeZoneShown = 0;
    GtkButton* unitToggleBtn;
    
    // Rally gauge
    GtkWidget* rallyGaugeDrawingArea;
    // What the 10 ms tick decided (rally_tick.h): ahead/behind, the driver page, the tone, the
    // Stage arrival, this tick's beeps ... The displays and the phone telemetry draw / send
    // these; `tickMem` is what the tick carries from one run to the next.
    TickDecision tick;
    TickMemory tickMem;
    std::string driverTrialTargetText;   // km/h, 2 dp
    std::string driverTrialSpeedText;    // km/h, 2 dp
    // The greek / surrey speed and target as the driver page writes them (the labels and the
    // phone both take these; nothing reads the text back off a label).
    std::string driverSpeedText = "--.-", driverTargetText = "--.-";
    // What the driver page last drew, word for word, so the phone shows the same (never recomputes).
    struct DriverShown {
        std::string speed, target, targetCap, totalAvg, tripAvg, digital, totalDist, tripDist;
        int zone = 0;
        double maxSeconds = 3.0;
    } driverShown;
    // What the gauge showed when it was last queued for a redraw (ui_driver.cpp queueGaugeRedrawIfChanged).
    GaugeDrawInputs gaugeDrawn;
    bool gaugeDrawnValid = false;
    // RB-DRV-19: which compact page the driver gauge draws (one set of sizes and positions).
    // (tick.driverPage; the same numbers as DRIVER_PAGE_* in rally_tick.h / driver_trial.h.)
    static constexpr int DP_CLASSIC = DRIVER_PAGE_CLASSIC, DP_ITALIAN_TRIAL = DRIVER_PAGE_ITALIAN_TRIAL,
                         DP_ITALIAN_NAVI = DRIVER_PAGE_ITALIAN_NAVI, DP_GREEK_STAGE = DRIVER_PAGE_GREEK_STAGE,
                         DP_GREEK_IDLE = DRIVER_PAGE_GREEK_IDLE;
    GtkLabel* calDistUnitLabel = nullptr;     // calibration page: "m" or "mi" beside the two distances
    GtkLabel* calActualUnitLabel = nullptr;
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
    GtkWidget* menuScreen = nullptr;  // Nero2.0 full-screen menu page
    GtkWidget* settingsScreen = nullptr;   // Nero2.0 Settings page
    GtkWidget* setupScreen = nullptr;      // Setup page, opened by the spanner key on Settings
    GtkWidget* setupDisplayRow = nullptr;  // one card per connected display
    GtkLabel* setupStatusLabel = nullptr;
    GtkLabel* settingsFpsLabel = nullptr;   // under the spanner on Settings
    GtkLabel* settingsCpuLabel = nullptr;
    GtkWidget* setupWifiRow = nullptr;     // one key per saved Wi-Fi network
    GtkLabel* bluetoothAudioLabel = nullptr;
    GtkWidget* setupBtConnectBtn = nullptr;
    GtkWidget* setupBtDisconnectBtn = nullptr;
    // Nero2.0 Date/Time and Calibration pages
    GtkWidget* sensor1Btn = nullptr;
    GtkWidget* sensorBothBtn = nullptr;
    GtkWidget* dtAreas[4] = {};        // rally, computer, phone, offset drawings
    GtkWidget* dtSetFromPhoneBtn = nullptr;
    // The phone's clock, as last reported over Wi-Fi: its time at the report
    // and the computer's time then. 0 = no phone has reported.
    int64_t phoneClockMs = 0;
    int64_t phoneClockSeenMs = 0;
    // CSV log of every counter poll while Sensor logging is on (debug_log.h).
    DebugLog* debugLog = nullptr;
    // CSV of the running stage, once a second while Stage data logging is on.
    StageLog* stageLog = nullptr;
    int64_t stageLogLastSampleMs = 0;
    HomeUi* home = nullptr;           // Nero2.0 Home page; null in single-display mode
    // Freeze (Home): the co-pilot readouts hold their figures. The driver
    // display and the phone never see it.
    // How the running stage started (auto or manual, when, for which stage) is in RallyState
    // (started_by_auto, auto_fired_time_s, auto_slot): it is saved, so a restart keeps it.
    
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
    GtkWidget* nextPrevBtn;  // "next" (the name predates the separate prev)
    GtkWidget* prevBtn;
    // Reset Total pressed with a stage running (usership 6): the counters and
    // rally time AT THE PRESS, applied if the crew confirm -- the distance
    // zero is the press, not the confirm. Cleared when the question is
    // answered, either way.
    bool totalResetPending = false;
    uint64_t totalResetCntr1 = 0;
    uint64_t totalResetCntr2 = 0;
    int64_t totalResetPressMs = 0;
    GtkWidget* adjZeroBtn;
    // Read-only stage panel on the main navigator screen, and the text it is
    // currently showing -- compared each tick so the label is only touched
    // when something actually changed, rather than 100 times a second.
    GtkLabel* stageStatusLabel = nullptr;
    std::string stageStatusShown;

    GtkLabel* webUrlLabel = nullptr;
    GtkWidget* setupForceBtn[2] = {nullptr, nullptr};   // Setup page: Force single display off / on
    GtkWidget* webQrArea = nullptr;
    RallyWebServer* webServer = nullptr;
    // Phone upload stage: the file last checked, and the stages just loaded.
    std::string uploadCheckId;
    std::string uploadCheckText;
    std::vector<int> uploadLoaded;
    // Phone Setup: last status line, and the saved Wi-Fi list after the phone asks.
    std::string setupStatus;
    std::string setupWifiJson;
    std::string setupDisplaysJson;   // the box's displays for the phone Setup sheet
    
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

    // Beep Assist bookmarks and the italian press-warning beep state are in tickMem (rally_tick.h):
    // runtime only, re-derived from the distance already travelled after a restart.
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
    GtkLabel* calibrationCurrentLabel;  // "Using Calibration N pulses/KM. Reset to"
    GtkEntry* resetPulsesEntry;  // operator-entered target for RB-CAL-03's reset
    GtkLabel* calibrationClockLabel;  // rally clock, shares the title row
    // RB-CAL-06: Prop/Wheel RPM (pulses box on the "4. Save Calibration" line,
    // reading under the keypad), and the reading it last showed --
    // compared each tick so the label is only touched when the figure changes.
    GtkEntry* propPulsesEntry = nullptr;
    GtkLabel* propRpmLabel = nullptr;
    std::string propRpmShown;

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
    // Rally-clock trim (+0.1 / -0.1). The timer drives press-and-hold
    // auto-repeat; trimRepeatSteps carries the held button's direction so
    // one pair of handlers serves both buttons.
    guint trimRepeatTimer = 0;
    int trimRepeatSteps = 0;
    
    // Auto Start setup screen
    GtkWidget* autoStartScreen;
    GtkLabel* autoStartRallyClockLabel;
    GtkLabel* autoStartTimeLabel;
    GtkEntry* autoStartTimeEntry;
    GtkWidget* autoStartKeypad;
    
    // Driver countdown overlay
    GtkWidget* countdownOverlay;
    GtkWidget* faultCopilot = nullptr;   // "counter fault" warning, centre screen; a tap clears it
    GtkWidget* faultDriver = nullptr;
    // Next segment's target speed, smaller, under the Target value in the
    // wide driver layout. The compact layout draws its own with cairo.
    GtkLabel* nextTargetSpeedLabel = nullptr;
    GtkLabel* countdownLabel;
    // Line under the countdown box saying whether the pending autostart
    // permits an early departure. Its own widget rather than more text in the
    // countdown label, so the box keeps its fixed monospace geometry.
    GtkLabel* earlyDepartureLabel = nullptr;
    // The countdown box AND the line under it. Shown/hidden as one; the code
    // used to reach the box via the label's parent, which no longer reaches
    // far enough now there is a second row.
    GtkWidget* countdownContainer = nullptr;
    
    // Tone generator for speed adjustment alerts
    ToneGenerator* toneGen = nullptr;
    // RB-WEB-02: the cadence toneGen was last set to is tick.currentTone, so the phone's
    // telemetry can play the same ahead/behind tone.
    // The last Beep Assist beep to fire, for the phone (RB-WEB-06). Runtime
    // only: seq 0 at every app start, so a phone connecting before the first
    // waypoint has nothing to replay.
    BeepEvent lastBeep;
    // The box's own words for a phone command it has just refused (a figure out of place, a start in the past ...):
    // set by webHandleCommand, sent to that phone alone and cleared by the web server, so the phone says what the
    // box would have said (owner, 2026-11-15).
    std::string webNotice;
    
    int updateCount = 0;
    int64_t lastUpdateCountTime_ms = 0;

    // Sim Control Panel (3rd display, dev/testing only — created only when
    // RALLY_SIM_I2C=1). Drives the simulated counters in real time.
    SimCounter* simCounter1 = nullptr;
    SimCounter* simCounter2 = nullptr;
    GtkWidget* controlWindow = nullptr;
    GtkWidget* controlSpeedButtons[7] = {};  // 20,25,30,35,40,45,50 km/h
    GtkWidget* controlStartBtn = nullptr;
    GtkWidget* controlStopBtn = nullptr;
    GtkLabel* controlStatusLabel = nullptr;
    double controlSpeedKph = 0.0;

    // Beep Assist has no audible feedback in the sandbox (no ALSA device),
    // so this label stands in for the sound: it flashes briefly whenever a
    // navigation or timing beep fires, naming which mode triggered it.
    GtkLabel* beepFlashLabel = nullptr;
    // The flash's own hide-timer, so a second beep arriving inside the window
    // replaces it rather than adding a second timeout that hides the label
    // early. 0 = not currently showing.
    guint beepFlashTimer = 0;
};
#endif // RALLY_NO_GTK

#endif // RALLY_TYPES_H
