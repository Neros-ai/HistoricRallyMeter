#ifndef RALLY_STATE_H
#define RALLY_STATE_H

#include <cstdint>
#include <vector>
#include "rally_types.h"

class RallyState {
public:
    bool units = false;  // false = KPH, true = MPH
    long calibration = 600000;  // mm per 1000 counts
    bool counters = true;  // false = one gearbox, true = two wheel
    uint64_t total_start_cntr1 = 0;
    uint64_t total_start_cntr2 = 0;
    int64_t total_start_time_ms = 0;  // milliseconds since epoch
    uint64_t trip_start_cntr1 = 0;
    uint64_t trip_start_cntr2 = 0;
    int64_t trip_start_time_ms = 0;
    uint64_t segment_start_cntr1 = 0;
    uint64_t segment_start_cntr2 = 0;
    int64_t segment_start_time_ms = 0;
    long segment_current_number = -1;  // -1 = no segment
    long rallyTimeOffset_ms = 0;  // offset in milliseconds
    long ahead_behind_zero_offset_ms = 0;  // manual offset for driver's ahead/behind display

    // Manual distance corrections applied to the Total and Trip readouts, in
    // centimetres (the unit distances are computed in). Set from the co-pilot
    // main screen's -10 / set / +10 controls to dial out wheel slip or a
    // roadbook discrepancy without resetting the counter and losing the
    // elapsed time. Each is cleared when its own counter is reset or a stage
    // starts -- even though the -10/+10 buttons that set them always write
    // to both together (a wheel-slip correction affects the one shared
    // measurement both readouts are derived from).
    long total_distance_adjust_cm = 0;
    long trip_distance_adjust_cm = 0;

    // Seconds since 1/1/2020, 0 = not set. Seconds, not minutes: the setup
    // screen accepts HH:MM:SS and a minute-resolution store threw the seconds
    // away, firing early by up to 59s on the one screen that exists to start
    // at an exact time.
    uint64_t auto_start_rally_time_s = 0;
    // True when the pending autostart was armed by "Autostart at next minute",
    // which zeroes DISTANCE at the moment it is armed and the CLOCK at the
    // appointed minute -- so distance covered before the due time still counts
    // toward the stage and its average speed. False for "Set Autostart", where
    // distance and clock both zero together at the target time. Persisted, so
    // an app restart between arming and the minute still starts the right way.
    bool auto_start_early_departure = false;

    // The roadbook as EDITED: what the stage-setup screen shows, what a
    // memory slot stores and recalls, what the phone's segment list edits.
    // Nothing here affects a stage that is already running or armed.
    std::vector<Segment> segments;

    // The roadbook the running stage is actually calculating against -- a
    // snapshot of `segments` taken at the instant the stage STARTS (its clock
    // zeroes), and the vector `segment_current_number` indexes.
    //
    // Everything up to that instant counts, including edits made while an
    // autostart counts down: arming is not starting. Everything after it
    // lands on the next stage.
    //
    // Two vectors rather than one because the ideal-position maths
    // re-integrates the whole roadbook from segment 0 on every frame. With a
    // single shared list, editing a segment or recalling a memory slot to
    // prepare the NEXT stage rewrote the running stage's history: the crew
    // drove twenty minutes against one set of targets and the box then
    // insisted they should have been driving another. Snapshotting means an
    // edit lands on the next stage, which is the only stage it can honestly
    // apply to.
    std::vector<Segment> stage_segments;
    // False while a stage is genuinely under way -- started, and not yet
    // driven past the end of its last segment. While it is false the
    // snapshot is frozen; once it is true an edit or a memory recall adopts
    // the roadbook straight away, so the crew can see what they have just
    // set instead of staring at the stage they have already finished.
    // Starts true: nothing is under way until something starts.
    bool stage_complete = true;

    // Which memory slot the loaded roadbook was recalled from, 1-5, or 0 for
    // "not from a slot" -- never recalled, or edited since. Tracked and
    // persisted but not currently shown anywhere: the status panel dropped
    // it because "Stage: N" was the widest field on the line and pushed the
    // rest of the row across.
    int last_memory_slot = 0;
    // False when the config file that was loaded predates stage_segments, so
    // load() can seed the snapshot from `segments` and leave a stage running
    // across the upgrade calculating exactly as it did before. Not persisted;
    // load() decides it per file.
    bool stage_segments_recorded = true;
    
    // Up to 5 memory slots for storing/recalling a whole stage setup.
    // A slot carries its Beep Assist waypoints alongside its segments: the
    // two are one setup, and recalling segments while leaving the PREVIOUS
    // stage's waypoints loaded is a trap rather than a feature. Beep Assist's
    // modes and lead-ins stay global -- they are crew preferences, not stage
    // data. empty() is defined so the many "is this slot populated?" checks
    // read the same as they did when a slot was a bare vector.
    struct MemorySlot {
        std::vector<Segment> segments;
        // Metres from the Total counter's zero. Calibration-independent, so
        // unlike segments these are never recalculated on a calibration
        // change.
        std::vector<double> beep_waypoints_m;
        // True when this slot's waypoint list is authoritative -- it was
        // stored in this session, or loaded from a config file that carried
        // a "memory_N_waypoints" key for it. A slot written before Beep
        // Assist existed has no such key, so its empty vector means
        // "unknown", not "deliberately none": recalling one must leave the
        // operator's live waypoint list alone rather than wipe it. Not
        // persisted -- load() decides it per file.
        bool waypoints_recorded = true;

        bool empty() const { return segments.empty(); }
        void clear() {
            segments.clear();
            beep_waypoints_m.clear();
            waypoints_recorded = true;
        }
    };

    static constexpr int MAX_MEMORY_SLOTS = 5;
    MemorySlot memory_slots[5];
    
    // Alarm: co-pilot sets distance alarm that rings a doorbell
    int alarm_distance_km = 0;          // 0 = no alarm active
    int64_t alarm_target_counts = 0;    // absolute count target from total_start

    // Beep Assist: operator-entered waypoints that sound a short beep as they
    // are reached, so the co-pilot's eyes can stay on the roadbook instead of
    // the odometer. Waypoints are absolute distances from the Total counter's
    // zero -- the way a roadbook lists them -- so a mis-entered value corrupts
    // one waypoint rather than every one after it.
    bool beep_assist_enabled = false;
    std::vector<double> beep_waypoints_m;
    double beep_advance_m = 0.0;      // navigation mode: beep this far before
    double beep_advance_s = 0.0;      // timing mode: beep this long before
    bool beep_navigation_mode = false;
    bool beep_timing_mode = false;

    // Force single-display mode even when multiple screens exist
    bool force_single_display = false;

    // Ahead/behind tone. tone_enabled is a master on/off (default true,
    // matching today's always-on tone). simple_tone_mode picks which
    // algorithm plays when enabled: false = Type 1, the existing
    // speed/arrow-based tone (default); true = Type 2, the alternative
    // time-error-only tone (constant, +/-0.2s quiet band, 0.2s update
    // hysteresis, ignores speed/recovery distance).
    bool tone_enabled = true;
    bool simple_tone_mode = false;

    // Embedded web server for phone browsers
    bool web_enabled = true;
    int web_port = 8080;
    
    // Driver window position/size (remembered across sessions)
    int driver_window_x = -1;      // -1 = not set
    int driver_window_y = -1;
    int driver_window_width = 1280;
    int driver_window_height = 400;
    int driver_window_monitor = 0;
    
    RallyState();
};

#endif // RALLY_STATE_H
