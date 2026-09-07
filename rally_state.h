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
    // Seconds since 1/1/2020, 0 = not set. Seconds, not minutes: the setup
    // screen accepts HH:MM:SS, and storing at minute resolution threw the
    // seconds away -- so the one screen that exists to start at an exact
    // time could fire up to 59s early.
    uint64_t auto_start_rally_time_s = 0;
    // True when the pending autostart was armed by the Stage Go dialog's
    // "Autostart HH:MM:00" button, which zeroes DISTANCE the moment it is
    // armed and the CLOCK at the appointed minute -- so ground covered on
    // the way to the line still counts toward the stage and its average
    // speed. False for "Set Autostart", where distance and clock zero
    // together when it fires. Persisted, so an app restart between arming
    // and the minute still starts the right way.
    bool auto_start_early_departure = false;
    // The roadbook as EDITED: what the stage-setup screen shows, what a
    // memory slot stores and recalls. Nothing here affects a stage that is
    // already running.
    std::vector<Segment> segments;

    // The roadbook the running stage is actually judged against -- a
    // snapshot of `segments` taken when the stage STARTS (its clock zeroes),
    // and the vector `segment_current_number` indexes.
    //
    // Two vectors rather than one because the ideal-position maths
    // re-integrates the whole roadbook from segment 0 on every frame. With a
    // single shared list, editing a segment or recalling a memory slot to
    // prepare the NEXT stage rewrote the running stage's history: the crew
    // drove twenty minutes against one set of targets and the box then
    // insisted they should have been driving another.
    std::vector<Segment> stage_segments;
    // False when the config file that was loaded predates stage_segments, so
    // load() can seed the snapshot from `segments`. Not persisted.
    bool stage_segments_recorded = true;

    // False only while a stage is genuinely under way -- started, and not
    // yet driven past the end of its last segment. While it is false the
    // snapshot is frozen; once true an edit or a recall is adopted straight
    // away, so the crew see what they have just set instead of the stage
    // they have already finished. Starts true: nothing is under way until
    // something starts.
    bool stage_complete = true;
    
    // Up to 5 memory slots for storing/recalling segment setups
    static constexpr int MAX_MEMORY_SLOTS = 5;
    std::vector<Segment> memory_slots[5];
    
    // Alarm: co-pilot sets distance alarm that rings a doorbell
    int alarm_distance_km = 0;          // 0 = no alarm active
    int64_t alarm_target_counts = 0;    // absolute count target from total_start

    // Force single-display mode even when multiple screens exist
    bool force_single_display = false;

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
