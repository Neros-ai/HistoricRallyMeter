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

// Forward declarations
class ICounter;
class SimCounter;
class RallyState;
class CounterPoller;
class ToneGenerator;
class RallyWebServer;
class DebugLog;
class StageLog;
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
    double aheadBehindSeconds = 0.0;
    double smoothedSpeed = -1.0;      // EMA-filtered current speed for display
    double segmentProgress = 0.0;     // fraction (0..1) of the current segment driven
    bool inSegment = false;           // true while within the current segment's distance
    // RB-DRV-18: inside an italian pc / pm the compact gauge draws this layout instead.
    DriverTrialView driverTrial;
    std::string driverTrialTargetText;   // km/h, 2 dp
    std::string driverTrialSpeedText;    // km/h, 2 dp
    // The greek / surrey speed and target as the driver page writes them (the labels and the
    // phone both take these; nothing reads the text back off a label).
    std::string driverSpeedText = "--.-", driverTargetText = "--.-";
    // Trip and Total averages in the box's units, unrounded (the driver page writes them at 2 dp).
    double driverTripAvg = 0.0, driverTotalAvg = 0.0;
    // What the driver page last drew, word for word, so the phone shows the same (never recomputes).
    struct DriverShown {
        std::string speed, target, targetCap, totalAvg, tripAvg, digital, totalDist, tripDist;
        int zone = 0;
        double maxSeconds = 3.0;
    } driverShown;
    // RB-DRV-19: which compact page the driver gauge draws (one set of sizes and positions).
    static constexpr int DP_CLASSIC = 0, DP_ITALIAN_TRIAL = 1, DP_ITALIAN_NAVI = 2,
                         DP_GREEK_STAGE = 3, DP_GREEK_IDLE = 4;
    int driverPage = DP_GREEK_IDLE;
    double driverTotalM = 0.0, driverTripM = 0.0;   // distances with their corrections, metres
    int driverArrivalKph = -1;      // speed to reach the next stage with a minute spare (kph), -1 = none
    long driverStageNo = 0;         // the loaded stage, 1-99
    // Greek stage page: the average speed over the run being driven -- from the box where the
    // speed last changed, through any boxes that carry it on. kph, valid once 0.5 s and > 0 m in.
    // runSegIndex / runStart*: where that run began (set by the display tick; runtime only).
    long runSegIndex = -2;
    uint64_t runStartCntr1 = 0, runStartCntr2 = 0;
    int64_t runStartMs = 0;
    long runStartAdjustMm = 0;
    double driverSubAvgKph = 0.0;
    bool driverSubAvgValid = false;
    double driverRowS = 0.0;        // italian navi row: its duration at its target speed
    bool driverMinutesValid = false;
    long driverMinutes = 0;         // italian navi: minutes ahead (+) / behind (-)

    // Runtime latch for the alternative time-error-only tone
    // (simple_tone.h). Deliberately not persisted -- restarting the app
    // with a fresh latch is correct, matching aheadBehindSeconds above.
    SimpleToneState simpleToneState;
    ArrowToneState arrowToneState;

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
    // The autostart moment (seconds since 2020) that has just started the stage
    // that is running, kept so Home goes on showing "auto hh:mm:ss" -- the time
    // the stage actually began -- instead of reverting to "manual" and the
    // memory's own start time the moment the target is spent. A start now
    // (the flag-and-stopwatch key) also records the moment of the press, but
    // Home no longer labels that one "auto" (startedByAuto is false). 0 = none;
    // the stage ending drops it. Runtime only.
    uint64_t autoFiredTime_s = 0;
    // Which memory (1-5) the armed or just-fired autostart belongs to: Home shows
    // that time only beside that stage, and every other stage shows its own start
    // time from the Segments page.
    int autoSlot = 0;
    // True only while the running stage was started by an autostart (either
    // branch); a start now clears it. Home reads "auto" or "manual" from it.
    // Runtime only: after a restart it is false.
    bool startedByAuto = false;
    
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
    GtkLabel* alarmCountdownLabel;
    // Read-only stage panel on the main navigator screen, and the text it is
    // currently showing -- compared each tick so the label is only touched
    // when something actually changed, rather than 100 times a second.
    GtkLabel* stageStatusLabel = nullptr;
    std::string stageStatusShown;
    GtkWidget* alarmClearBtn;
    int64_t alarmSoundStartTime = 0;    // when doorbell started (0 = not ringing)

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
    // Press-warning beeps (italian): the row being counted, the pc's tube time when
    // it has one, and the last seconds-to-line reading, so each mark is heard once.
    long pcBeepRow = -1;
    int64_t pcBeepTrialMs = -1;
    double pcBeepPrevS = 0.0;
    // 100 m beep on a non-pc row: the row, and the last metres-to-go reading.
    long distBeepRow = -1;
    double distBeepPrevM = 0.0;
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
    bool autoStartTriggered = false;
    
    // Tone generator for speed adjustment alerts
    ToneGenerator* toneGen = nullptr;
    // RB-WEB-02: the cadence toneGen was last set to, kept so the phone's
    // telemetry can play the same ahead/behind tone.
    ToneCadence currentTone;
    // The last Beep Assist beep to fire, for the phone (RB-WEB-06). Runtime
    // only: seq 0 at every app start, so a phone connecting before the first
    // waypoint has nothing to replay.
    BeepEvent lastBeep;
    
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
