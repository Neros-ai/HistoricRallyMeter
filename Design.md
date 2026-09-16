## Environment
This environment is a Raspberry Pi 5 with 4GB memory connected to 2 LSI ls7866c 32-bit counters (CNTR_1, CNTR_2) on I2C bus 1 at addresses 0x70 and 0x71 The 32-bit counter register 0x07 and its value is big-endian. the application should be 
    GTK3-based GUI window application with two display windows. 
    Use high-resolution chrono timers for accurate measurement
    C++  version 20

## Displays

The application presents two windows, each intended for its own in-car display panel:

| Window | Purpose | Required resolution |
|---|---|---|
| **Co-pilot display** | TwinMaster, segment setup, calibration, date/time screens | Exactly 1280x400 (or 400x1280 rotated) |
| **Driver display** | Speed gauge and stage readouts | 1280x400 **or** 800x480 (or their rotated equivalents). Uses the compact layout on 800x480 (see "Driver Display - Compact Layout") |

Definitions used throughout this section:
- **Small display**: a monitor whose resolution is exactly 1280x400, 400x1280, 800x480, or 480x800.
- **Co-pilot-capable display**: a small display of exactly 1280x400 (or 400x1280). The co-pilot window never uses an 800x480 panel.
- **Development setup**: an HDMI 2K/4K desktop monitor plus one or two small displays. When two small displays are attached the application must use them for the two windows and leave the desktop monitor free.

### Display Detection

1. Read DRM connector information from `/sys/class/drm/` (entries such as `card1-DSI-2`) to obtain each connector's name, connection status, and mode resolution.
2. Match each GDK monitor to a DRM connector by resolution.
3. Collect all small displays and sort them by connector priority: **DSI before HDMI, then by connector number, lowest first** (e.g. DSI-1, DSI-2, HDMI-A-1, HDMI-A-2). A monitor that cannot be matched to a connector sorts last.

### Display Assignment

Assignment happens at startup, in this order:

0. **Single-display mode**: if exactly one monitor exists and it is 1280x400, only the co-pilot window is shown, fullscreen on that monitor. The driver window is not shown at all. In this mode the TwinMaster screen's right-hand alarm panel is replaced by the embedded compact driver display (see "TwinMaster Screen - Single-Display Mode"). The remaining rules do not apply.
1. **Co-pilot window** takes the highest-priority **co-pilot-capable** display and opens fullscreen on it.
   - Fallback: if no co-pilot-capable display exists, it opens as a normal 1280x400 window (typically on the desktop monitor during development).
2. **Driver window** takes the highest-priority remaining small display (1280x400 or 800x480, never the one assigned to the co-pilot) and opens fullscreen on it.
   - Fallback: if no small display remains, it opens as a window using the size remembered from the previous run (default 1280x400), centered on the remembered monitor. If the remembered monitor no longer exists or is the co-pilot's monitor, the first other available monitor is used.

### Window Persistence (driver window fallback only)

- The driver window's size and monitor index are saved to the config file when the window is moved/resized and on shutdown.
- Window *position* cannot be saved/restored on Wayland due to compositor security limitations, hence centering on the saved monitor at startup.
- Nothing is persisted when both windows are fullscreen on small displays.

### Preventing Inactive Window Dimming

By default, GTK3 applies a "backdrop" state to unfocused windows, which dims the content and can make text harder to read. Since this application uses two windows that need to be visible simultaneously, this dimming effect is undesirable.

**Solution: GTK CSS Override**

Create `~/.config/gtk-3.0/gtk.css`:
```css
/* Prevent GTK from changing unfocused (backdrop) window appearance */
/* Keep the same colors as when focused */

.background:backdrop {
    background-color: @theme_bg_color;
}

*:backdrop {
    color: @theme_fg_color;
    -gtk-icon-effect: none;
}

label:backdrop,
entry:backdrop,
button:backdrop {
    color: @theme_fg_color;
}
```

This CSS override tells GTK to use the normal theme colors (foreground and background) for windows in the backdrop state, rather than the dimmed variants. The change takes effect immediately for newly launched GTK3 applications.

An example file `gtk-example-gtk.css` is included in this project - copy it to `~/.config/gtk-3.0/gtk.css`.

**Optional: Labwc Titlebar Theme**

The labwc compositor also styles inactive window titlebars differently. To make them match active windows, create `~/.config/labwc/themerc-override` with settings that copy active window colors to inactive. After changing, run `labwc --reconfigure`.

### Debug Configuration
VS Code/Cursor debug configuration is provided in `.vscode/`:
- `launch.json` - Debug configurations for main app and unit tests
- `tasks.json` - Build tasks (debug, release, test, clean)
- `c_cpp_properties.json` - IntelliSense configuration

Use `make debug` to build with debug symbols (`-g -O0`), producing `HistoricRallyMeter_debug`.

## Build Requirements

- GTK3 development libraries (`libgtk-3-dev`)
- Standard C++ compiler with C++20 support
- Linux I2C device interface
- Clear structure with only one class per file
- Application excutable and name should be HistoricRallyMeter

# Source code control
The system should be stored in github simonpapworth6742/HistoricRallyMeter repo

## Application Overview


The purpose of the Historic Car Regulation Rally meter is to enable drivers and co-pilots of classic cars to win regulation rallies, where the classic car must be driven over a multi-segment stage at set speeds to within 0.1 seconds of the measured time over distances varying from 2 km to hundreds of kilometres. All distances shown are calculated with the current calibration.


              The system will maintain several global variables backed by a local json file, loading them into global variables on start up and updating them and the global variables in memory via the various screens, sometimes with a save option and sometimes automatically such as Total / Trip resets. Functions should have all required global variables passed as parameters to enable unit testing simply. If the json file is missing or corrupted the values default as defined below or to the current count / time depending on the type of variable.

              Global variables: -

Boolean units – false = KPH (default), true = MPH
long calibration = see calibration below, defaults to 600000.
Boolean counters – false = One gearbox 32 bit counter CNTR_1, True= two wheel 32 bit counters CNTR_1 & CNTR_2, when set the number of counts from the total_start and trip_start is the average of CNTR_1 and CNTR_2
ulong total_start_cntr1 - Total distance start count for CNTR_1
ulong total_start_cntr2 - Total distance start count for CNTR_2
time total_start_time – the time to at least the nearest ms that the total distance counters was last reset – defaults to now on startup if not present
ulong trip_start_cntr1 - Trip distance start count for CNTR_1
ulong trip_start_cntr2 - Trip distance start count for CNTR_2
time trip_start_time – the time to at least the nearest ms that the trip distance counters was last reset – defaults to now on startup if not present
ulong segment_start_cntr1 - segment distance start count for CNTR_1
ulong segment_start_cntr2 – segment distance start count for CNTR_2
time segment_start_time – the time to at least the nearest ms that the last segment was started
long segment_current_number – the current segment of the stage so defining the current target average speed displayed. On startup/empty segments, segment_current_number defaults to -1; blank Seg/target/±/ETA/next line on drivers display.
 
long rallyTimeOffset – ms offset of rally time to operating system time, defaults to 0
long ahead_behind_zero_offset_ms - ms offset (+ or -) of the drivers ahead/behind caculation,defaults to zero and on stage-go. Set in the twinmaster display.
ulong auto_start_stage_at_rally_time - the date time in munites the stage should automatially be started in rally time, stored as an offset from 1/1/2020.
structure segment[]  - Stage segments contain target speed over distance segments of the stage, and if manual or automatic progression to the next segment is required. Defaults to no segments.
double target_speed_kph - the actual speed requested for this segment, this does not change when the calibration changes
double target_speed – stored in number of counts per hour via the calibration (high precision floating point). This is recaculated when calibration changes
double distance_m - the actual distance in meter entered, this does not change when the calibration changes
double distance – number of counts for the segment (high precision floating point).This is recaculated when calibration changes
Boolean autoNext – True =  when the distance of this segment has been reached the next segment is started automatically, changing the segment_current value and segment_start counter as well as resetting the trip counter values, false = the next segment button on the co-pilots TwinMaster display must be pressed to advance to the next segment, setting the segment_current, segment_counters and Trip counters
 

The system has two counters available CNTR_1 and CNTR_2, when configured to use one gearbox counter then the distance since total, trip or segment  will be the value of the current CNTR_1 minus the total / trip / segment start_cntr1. If configured to two wheel then the distance since total, trip or segment will be ( (CNTR_1 - the total / trip / segment start_cntr1) + (CNTR_2 - the total / trip / segment start_cntr2) ) then divided by 2. Use integer maths, this caculated counter should be called CNTR_A

Calibration, As the wheels / gearbox rotate and the car moves forward, the amount the car travels in meters per counter increment has to be set via calibration. It is expected that there could be as few as one counter increment per wheel revolution and as many as sixteen. To ensure accuracy the calibration will be stored as the number of millimetres travelled per 1000 counts. Meters travelled = (count_diff * calibration) / 1000 / 1000. Keeping the calibration as a larger number enables integer maths to calculate speeds and distance travelled in centimetres. When updating the calibration new_cal = (input_meters * 1000 * 1000) / total_count_diff (to match mm/1000 counts).

 

Calculation of speed and display of speed / target speed. Taking account of the formula for two wheel counting vs gearbox counting the difference in counted distance travelled from a (total / trip /segment start multiplied by the calibration) and divided by 10000 = centimetres travelled. centimetres travelled divided by the time taken since the start gives a speed, and allow for integer maths for all but the final conversion to KPH or MPH, all speeds will be calculated for display from the counts travelled / time taken and then converted to KPH or MPH. There are 100,000 cm in a kilometre and 160,934 cm in a Mile. if current speed is 100 km/h, switching to MHP will display on next update "62.14" mph


As the car is moving the counters will be polled regularly, but not more than every 5ms, if polled in less than 5ms the function will return the same result as last time. When a segment distance has been covered and autoNext is enabled the segment should move to the next segment, or subsequent segment if polling interval means more than one segment has been passed, if there is one.

Wherever the RallyClock is shown it is always the operating system time adjusted for the rallyTimeOffset by adding it. All times should be shown in 24 hour format.

The application will have two display windows, one for the driver and one for the co-pilot, each window is a separate desktop window, the pilot’s display window has a single screen, while the co-pilots display has several screens, the default being the TwinMaster screen.


calculation of current speed, with too little time passed since a start any speed calc will be too inaccurate to be useful, therefor within the polling loop of the counters a count should be remembered for about 2 seconds, by placing it into an array[10] of counter values and time polled, simply done on first poll the time of the poll and the value of the poll should be stored in the first position of the array. Each poll if more than 0.2 second has passed since the time in the first position of the array, then the array should be push down one, the last value lost, and the current value and time stored in the first array position. The 10th value of the array count and time should be available via class properties, to be used to calculate the current speed compared to the most recent poll, and not the value in the array position 1. If the time of the 10th position of the array is zero / empty / blank then the current speed should be shown as “--.—". Give a 20% time tolerance when checking the 10th position of the array contains a time over 80% of the total sampling period (i.e., age >= 1280ms, which is 80% of 2s minus 20% tolerance). 

when any button is pressed make a "soft beep" sound for feedback

**_Drivers display Window (1280 x 400) - dark theme only_**

The drivers display window is wide (1280px) and shallow (400px). It shows the average speed since the last reset of the Total, the current speed calculated from approximately the last 10 seconds of driving, the average speed since the last Trip reset, and the average speed since the start of the current segment. The target speed for the current segment and how many seconds ahead or behind target average speed by calculating how many counts difference there is between the actual count now and the count that it should be based upon the time since stage start taking account of the differing speeds in segments already completed and the target speed for the current segment, there is also an ahead_behind_zero_offset_ms value which is a simple addtion to the actual ahead/behind value. Along with the ETA = remaining segment distance / (last-10s average speed) to the next segment. If there is no current segment defined or more than 1000m past end of the last segment, then display "--.--" for Seg. For the next segment line of the display hide it if there is no next segment and if last-10s speed = 0: '--.--'; negative remaining: 'Over by xx:xx:xx'.

Seconds ahead/behind formula (high precision): ideal_counts = (time_ms_since_segment / 3600000.0) * target_counts_h; diff = actual - ideal; seconds = diff / (target_counts_h / 3600.0). positive numbers means travelling too fast. All target speed and ETA calculations use high precision (double) floating point arithmetic throughout. If more than +- 0.1 ahead/behind then after the seconds ahead/behind value calculate the increase in speed needed (acceleration/deceleration) to exactly match the target in the next 500 meters. Use up to 3 green up arrows to indicate the requirement to speed up, and up to 3 red down arrows to show the requirement to slow down next to the Current Speed. If speed adjustment needed is less than 3 kph show one arrow, between 3 and 10 show two arrows, and more than 10 show 3 arrows. 

Indicate the change in acceleration required green / red arrows to the driver with sound as well (provided within the stage) as the visual indicators, if driving to +- 0.1 seconds ahead / behind or greater than +-30 seconds emit no tone. Provided the distance is within the Stage using the same three acceleration brackets as the red / green arrows make 0.1 second tone with 0.1 second no tone, moving to 0.5/0.2 seconds and lastly 0.7/0.3 seconds.
The tones generated should be piano C6,C6,C6 when behind and F6,F6,F6 when ahead.The tone generator should apply a 5ms fade-in/fade-out envelope at every tone-to-silence and silence-to-tone transition.
The tones should only sound after the first 250 meters of the stage start and while withing the stage, once past the end of the last segment the tones should stop.

Updates per second is the number of times this display has been updated in a second, Rolling count of driver display render/update calls over the last full second.

If auto_start_stage_at_rally_time relative to rally time is in the future by less than 24 hours, then overlayed on the drivers display in 30px a count down clock "T- hh:mm:ss" with a thick white border, when zero seconds is reached the "stage go" rountine must be triggered as if the co-piliot had pressed and confirmed "stage go".

**Rally Gauge Display:**
The ahead/behind timing is displayed as a 180-degree semicircular gauge (rally gauge style):
look at the example guage in gaugepilot-rallymaster-display.png
- Zero (on target) at the top center (12 o'clock position)
- adjust the scale on the guage based upon the current number of seconds you are ahead/ behind, have three scales +- 5 minutes (red), +-10 seconds (yellow), +- 3 seconds (green)
- The amount ahead/behind should be large white text withing a white outlined box.
- red should have a red semi circle on the guage, and the amount ahead/behind should be shown as +-hhh:mm:ss
- yellow should have a yellow semi circle on the guage, and the amount ahead/behind should be shown as +-ss.s
- green should have a green semi circle on the guage, and the amount ahead/behind should be shown as +-ss.s
- the scale on the guage should not change too often, and after changing should wait two seconds before changing again, in effect debouncing.
- The needle is a narrow triangle (white filled) tapering from a 24px-wide base at the hub to a sharp point at the tip, with a black 1.5px centre line running from hub to tip, and a subtle drop shadow offset by 2px.
  The Needle is used to convey a lot of information to the driver, both the scale and the distance to the end of the this segment. When in green scale there shoud be a signal arrow "^" pointing along the needle 48px-wide, 20% of the needle length from the central pin this is known as the topmost arrow. If in the yellow scale there should be a second arrow under the topmost arrow and if the red scale a third arrow under the second.
  If within a segment there should be a small perpedicular line 20% of the needles length from the top of the needle 48px wide, the "arrows" for the scale should move closer to the line based on the percentage of the current segment driven or stay at their start position if not in a segment.
- The digital readout box (ahead/behind value) is positioned just below the needle hub
- The gauge provides an intuitive visual indication - needle pointing right means slow down, needle pointing left means speed up


``` Layout notes For display/windows for 1280x400 and larger (wide, shallow display):
+----------------------------------------------------------------------------------------------------------+
|   Current↑↑↑↓↓↓         Total                       |                      RALLY GAUGE                   |
|    xx.x                 xx.x                        |            -10s ←───┬───→ +10s                     |
|                                                     |                 ╱   │   ╲                          |
|   Target                Trip                        |               ╱     ▲    ╲                         |
|    xx.x                 xx.x                        |             ╱       ●     ╲                        |
|                                                     |            ╱    [±ss.s]     ╲                      |
|   fps: xxx  cpu: xxC   next: xx.xx in xxxm - mm:ss  |           ╱                   ╲                    |
+----------------------------------------------------------------------------------------------------------+
```


- Left side: Four speed values Current, Target, Trip, Total. Trip, Total with large fonts, Current with extra large font and target 70% of the size of Trip.
- Right side: Rally gauge with semicircular dial, target speed, and timing info
- Bottom row: Updates counter (fps) and cpu temperature on the left, next segment info in the center. The driver display has no unit (KPH/MPH) toggle button.
- The rally gauge should be prominently displayed as a graphical element
- Use large fonts for speed values as they are primary information for driver
- All elements arranged to maximize visibility for the driver
- Total and Trip should vertially align
- The speed up /slow down arrows should not effect the Total label position and should not effect the Current label position
- The number of digits displayed for any of the values should not effect their position the decimal point should remain the in same place.

``` Layout notes For display/windows for 800x480 (small 4:3 display):
+-----------------------------------------+
|  {target}                               |
|         -10s ←───┬───→ +10s             |
|            ╱     │     ╲                |
|          ╱       │ {tot} ╲              |
|        ╱         │         ╲            |
|      ╱ {current} ▲  {trip}   ╲          |
|    ╱             ●             ╲        |
|  fps:xxx       [±ss.s]           cpu:xxC|
+-----------------------------------------+
```
With less display area everything is compacted.
The gauge is identical in style to the wide layout above, and is reactive to the screen size.
Now the values displayed in the left pane of the wide layout are fitted within the gauge area:
- {current} is the current speed, top-left with no label; it is right-aligned to a fixed anchor wide enough for "###.#" so the digits never shift as the value changes.
- {tot} and {trip} are the values without labels.
- {target} sits left of the hub with a very small "Target" label above it, left-aligned with the value.
- Target, total and trip share the same font size (56px at full scale); current is slightly smaller (50px). All shrink with the gauge (scaled by gauge radius relative to the wide layout's 256px reference radius).
- The ahead/behind readout box auto-sizes to its text (minimum 130px wide); the font stays at full size for sunlight legibility.
- The needle hub has a white ring matching the needle for contrast.

Compact gauge geometry (fills the available area):
- The gauge radius is width-driven: half the drawing-area width minus 25px (just enough margin for the 18px bezel ring), capped by height minus 95px.
- The gauge is centred horizontally; the needle hub sits low, 75px above the bottom edge, leaving just enough room below the hub for the ahead/behind readout box and the footer line.
- The top of the gauge arc may extend up into (cut into) the target line and value area; the target text and clock are drawn on top of the gauge graphics.
- fps (bottom-left) and cpu (bottom-right) are drawn with their baseline in line with the bottom of the ahead/behind readout box.

The number of digits displayed for any of the values should not affect their position; the decimal point should remain in the same place.



**_Co-Pilots display window (1280 x 400) - dark theme only_**

The co-pilot display window is wide (1280px) and shallow (400px), same as the driver display. It has four screens:
1) Stage setup
2) Calibration
3) TwinMaster display (default)
4) Date and Time setup

Layout notes for 1280x400 (wide, shallow display):
- All layouts use horizontal arrangement to maximize width
- Buttons arranged in rows across the bottom
- Information displayed in columns or horizontal sections

---

**1) Stage Setup Screen**

Allows target speed, distance and AutoNext for multiple segments of a rally stage to be setup. 

```
+----------------------------------------------------------------------------------------------------------+
|                                        STAGE SETUP                                                       |
+----------------------------------------------------------------------------------------------------------+
|  Speed(KPH)      Distance(m)      Auto       Time                 Mem Set   Recall                       |
|    xx.xx          xxx,xxx           [Y]     mm:ss [del]             [1]    [1]                           |
|    xx.xx          xxx,xxx           [Y]     mm:ss [del]             [2]    [2]                           |
|    xx.xx          xxx,xxx           [Y]     mm:ss [del]             [3]    [3]                           |
|    xx.xx          xxx,xxx           [Y]     mm:ss [del]             [4]    [4]                           |
|    xx.xx          xxx,xxx           [Y]     mm:ss [del]             [5]    [5]                           |
|    xx.xx          xxx,xxx           [Y]     mm:ss [del]                                                  |
|    xx.xx          xxx,xxx           [Y]     mm:ss [del]            [clear memory]                        |
+----------------------------------------------------------------------------------------------------------+
|  New segment:  Speed [______] KPH    Distance [________________] m    Auto [_]    [add]       [back]     |
+----------------------------------------------------------------------------------------------------------+
```
The exisiting segments should have editable values and scroll if there are more than 5 rows, The font should be 18px. The scrollbar should be touch-friendly: slider 20px wide, trough 24px wide.
When editing any value a numeric entry keyboard should be shown on the right of the screen with a ";" button, buttons 72x58 pixels.
The New line at the bottom should have fonts 18px, speed entry boxe 130x40 pixels and distance 300x40 pixels, and buttons 80x40 pixels.
The distance allows mutiple values seperated by ";" to be entered, each semi-colon seperated value creates a segment at the speed defined.


The target speed is in KPH and the distance is in meters.
Counts per hour = (input_kph * 1000 * 3600) / (cal / 1000)
Changes in calibration have no effect on stored segment distance and speed values, but do update the stored counts for distance and speed.
Time is display only and caculated as the mm:ss required to cover the distance at the speed for that segment.

The memory storage allows for upto five stage setups to be remembered and then recalled on request, pressing the set button for the memeory number should copy the current segment setup into that memory position in the configuration file, after a conformation dialog (30px with white border) if the memory position is not empty. Pressing recall and a memory number should copy that memory position from the config file into the current setup and configuration, updating the display. Memory clear, after a conformation dialog box, should remove the memory sections from the configuration file.Buttons should be 66x43 pixels. 
If a memory location has a segments stored then the recall button should be a white background and black text.


---

**2) Calibration Screen**

The start button should zero the counts and total distance covered values on the display, and remember the actual CNTR_1 and CNTR_2 values
The display should update the distances and counters every 10 ms while this screen is shown, but not when it is not displayed.
save button should update the stored calibration as defined above.
```
+----------------------------------------------------------------------------------------------------------+
|                                        CALIBRATION                                                       |
+----------------------------------------------------------------------------------------------------------+
|   Total distance: xxx,xxx m  (counts caluated :CNTR_A   1:CNTR_1   2:CNTR_2)                             |
+----------------------------------------------------------------------------------------------------------+
|   Actual distance covered:  [______________] meters       [reset to 1m per pulse]                        |
+----------------------------------------------------------------------------------------------------------+
|   Currently set to {sensor 1 / both sensors}        [Set sensor 1]  [set both sensors and agv.]          |
+----------------------------------------------------------------------------------------------------------+
|     [start]                         [save]                                                      [back]   |
+----------------------------------------------------------------------------------------------------------+
```

Min input: 500m, Max input: 100,000m.
new_cal = (input_meters * 1000 * 1000) / total_count_diff
When editing any value a numeric entry keyboard should be shown on the right of the screen, the same as the stage setup screen.
When [save] is pressed the new calibaration should be changed in the rally_config file as well as recaculating all target_speed and distance in the segments and memeory.
The config and caculations should be updated when [Set sensor 1] or [set both sensors and agv.] is selected.
---

**3) TwinMaster Screen (Default)**

Two-column layout with bottom navigation row:

```
+-------------------------------------------------------------------+--------------------------------------+
| LEFT PANEL (70%)                                                  | RIGHT PANEL (30%)                    |
|                                                                   |                           hh:mm:ss   |
|  [Total]  xxx,xxx  m  mmm:ss                                      |  Alarm in [2] [3] [4]                |
|                                                                   |           [5] [6] [7]                |
|  [Trip]   xxx,xxx  m  mmm:ss                                      |           [8] [9] [10]               |
|                                                                   |          [11] [12] [13]              |
|  [Next/prev]   xxx,xxx  m   xxx kph                               |  x,xxx m to alarm  [clear]           |
+-------------------------------------------------------------------+--------------------------------------+
|   [stage go]      [segments]   [Adj. driver Zero (xx.xxs)]      [calibration]        [date/time]                  |
+----------------------------------------------------------------------------------------------------------+
```

Layout:
- GtkGrid for Total/Trip rows with aligned columns: heading | value | unit | reset | time
- The number of digits displayed for any of the values should not affect their position, the maximum number of meter to display is 999,999 
  before switching to km.
- Distances formatted with comma separators and fixed minimum width of 7 characters (e.g., "      0", "  1,234", "999,999")
- Time formatted as space-padded minutes (3 chars) + ":" + zero-padded seconds (2 chars), e.g., "  0:00", " 12:34", "120:00", 
    when more than four chars of minutes, switch to hours:minutes and if more than four chars of hours display "toolong".
- All fonts bold, all buttons have 2px solid white border for daylight visibility
- 5px border around the entire screen (all co-pilot screens)
- Two-column layout: left panel 70% width (~870px), right panel 30% width (~360px)
- Left panel:
  - Total/Trip in a GtkGrid (15px below segment info, 10px gap between rows):
    - Col 0: heading buttons "Total" / "Trip" / "Next" (48px bold monospace)
    - Col 1: distance value, right-aligned, 7-char width (88px bold monospace)
    - Col 2: unit "m" (48px bold monospace), bottom-aligned
    - Col 3: elapsed time mmm:ss (36px monospace, light grey #CCCCCC) or the speed of the next segment, vertically centred
- Segment info on the third line "Next" showing the distance to the next segment in meters and the speed of the next segment, if there are no segments the next line shows ---.--- and the speed shows ---. If past the end of the of the segments then the distance shows the negative meters past the end of the last segment and the speed shows "END". Next rounds up to the nearest meter so that Total/Trip are in sync to it as they round down.
  
- Right panel:
  - Rally clock (hh:mm:ss) at top, right-aligned (30px bold, minimum 8 chars wide)
  - Alarm buttons in four rows with 4px vertical gap — "Alarm in" label (20px) + [2]-[4] on first row, [5]-[7] on second row, [8]-[10] on third row, [11]-[13] on fourth row (22px font, 62x47px buttons)
  - Alarm countdown ("x,xxx m to alarm") and [clear] button below alarm buttons (28px white font #FFFFFF)



- Navigation buttons spread across full-width bottom row (20px font, 43px tall):

- stage go: conformation dialog (with 30px text and buttons with at least 20px between buttons), with "Auto start" option,  with yes reseting Total, Trip, and Segment (counters + start time), sets the driver's display gauge to green, and zero's the ahead_behind_zero_offset_ms. "Auto start" option should go to the "Auto start setup screen.
 
- segments: goes to Stage Setup

- Adj. driver Zero - Displays the current ahead_behind_zero_offset_ms in the label and at the point  pressed, the drivers/ahead behind value including the current   ahead_behind_zero_offset_ms is remembered so that it can be used in the conformation dialog (white border and 30px font, at least 20px between buttons) with the text "Adjust the ahead behind value by xx.xx seconds, currently xx.xx seconds" before being set in the ahead_behind_zero_offset_ms and changing the drivers ahead/behind guage. Along with Yes / No the dialog should have a "Reset to 0.0" option slighly distant from the Yes/No.
 
- calibration: goes to Calibration screen
 
- date/time: goes to Date/Time Setup screen

- Reset buttons: [Total] / [Trip]  are reset buttons and reset their respective counters and start time only

- [next/prev] button is only active when within 500m of the begining of a segment or the end of the segment, when it is within 500m of the end of a segement 
    the button displays "next", when it is within 500m of the start of a segment (not the first) it displays "prev" otherwise it displays "--->". The button allows the correction of distance of segment starts/ends, due to poor driving dicipline or mistakes in setting up the road book. When pressed within the 500m before a segment end then the segment distance should be reduced to the distance when the button was pressed. When press within 500m of the of the start of a segment (not the first) then the distance of the last segment should be extened to match the when the button was pressed.

- [clear] button is only visible when an alarm is active

- Distance alarm: co-pilot presses a km button (2-13) to set an alarm that many km ahead of the current total distance. The target is calculated in pulses and stored in the config file to survive Pi5 restart. When the total distance reaches the target, alarm.wav is played, then the alarm auto-clears after 5 seconds. The countdown ("x,xxx m to alarm") is shown in the right panel. The alarm check runs regardless of which co-pilot screen is visible. Press [clear] to cancel an active alarm.

**TwinMaster Screen - Single-Display Mode**

When the application is in single-display mode (exactly one monitor, 1280x400 - see "Display Assignment"), the right-hand alarm panel is replaced by the compact driver display:

- The right panel is widened to 430px (from 360px) so the gauge is height-limited rather than width-limited; the left panel gives up its fixed 870px minimum width and takes whatever width remains.
- The right panel contains the compact driver gauge layout, identical to the 800x480 driver layout: the gauge with needle and digital readout, with target, current, total and trip values drawn inside the gauge area, fonts scaled down with the gauge size, and the same compact gauge geometry (gauge fills the panel width, arc top may cut into the target line, fps/cpu in line with the readout box bottom).
- The rally clock (hh:mm:ss) is kept, drawn in the top-right corner of the gauge area at 28px scaled with the gauge (minimum 20px).
- Alarm buttons are unavailable in this mode, so new alarms cannot be set. An alarm persisted in the config from a previous run still fires: alarm.wav is played when the target is reached and the alarm auto-clears after 5 seconds; no countdown or [clear] button is shown.
- The left panel has the total/trip times hidden and the next target speed hidden inorder to allow the gauage to be bigger, bottom navigation row are unchanged.



---

**4) Date/Time Setup Screen**

```
On entry pre fill the date and time entry box's with the current date and time.
All fonts to be 20px

+----------------------------------------------------------------------------------------------------------+
|  DATE/TIME SETUP                                                                            [exit app]    |
+----------------------------------------------------------------------------------------------------------+
|  System Clock:  yyyy/mm/dd  hh:mm:ss                                                                      |
|                                          historicrallymeter.local:8080          7    8    9               |
|  Rally  Clock:  yyyy/mm/dd  hh:mm:ss.t    (or device IP if mDNS unavailable)    4    5    6               |
|                                                                                 1    2    3               |
|  Set Rally Clk: [yyyy/mm/dd] [hh:mm:ss]                                         /    0    :               |
|                                                                                [C]  [   <--   ]           |
|  Options:       [-0.1] [+0.1]                   +------------+                                            |
|  speed units          [ KPH ]                   |  QR code   |                                            |
|  force single display mode        ( o)          |  132x132   |                                            |
|                                                 +------------+                                            |
|                                              historicrallymeter.local:8080                                |
+----------------------------------------------------------------------------------------------------------+
|                        [Save new rally clock entry]                    [back]                            |
+----------------------------------------------------------------------------------------------------------+
```
Display a numeric keypad for entry on the right, it is a different keypad to other screens as it has "/" and ":" 
on it, but no ";" and ".".

- The [Exit app] button closes the application

- the Options menu
    force single display mode is a toggle button that sets a config value in the json file, and forces the use of single display only mode even if mutiple screens exist

    speed units is a button showing the current unit ("KPH" or "MPH"); pressing it toggles the `units` config value between KPH and MPH and saves it. This is the only place the units are changed (the driver display no longer has a unit toggle). All speed displays across the driver, co-pilot and web client follow this setting.

- **Phone web access** (shown only when `web_enabled` is true in the config):
    - Displays the full URL to the web client (scheme `http://`, host from mDNS name `historicrallymeter.local` with fallback to the device's current LAN IP address, port from `web_port`).
    - Renders a QR code encoding that same URL (minimum size ~120×120 px on screen, high contrast for scanning in daylight).
    - The QR code and URL update if `web_port` changes after save; they are omitted entirely when the web server is disabled.

**5) Auto Start Setup Screen**

```
On entry pre fill the time entry box's with the current auto start date and time.
All fonts to be 20px

+----------------------------------------------------------------------------------------------------------+
|                                       DATE/TIME SETUP                                   [exit app]       |
+----------------------------------------------------------------------------------------------------------+
|   Rally  Clock:  yyyy/mm/dd  hh:mm:ss        30px                                                        |
|                                                                                                          |
|   Auto Start:    yyyy/mm/dd  hh:mm:ss        30px  (Blank date time if not set)                          |
+----------------------------------------------------------------------------------------------------------+
|   Set Auto Start time within 3 Hours: [__________]    30px                                               |
+----------------------------------------------------------------------------------------------------------+
|  [Clear]                              [set]                                             [back]           |
+----------------------------------------------------------------------------------------------------------+
```
Display a numeric keypad for entry on the right, it is the same keypad layout as the Date/Time setup screen.

Only allow time to be entered in the 24 hour clock, display an error and don't allow the time to be set if more than 3 hours in advance.

Clear - sets the auto_start time to 0, making it in the past and therefor it has no further effect.
Set - sets the auto start time in the config file etc. recording the offeset as defined, the screen is updated to show the new values.

## Remote Web Access (mobile phones)

The application serves a small web application so that ordinary browsers on mobile phones, joined to the same subnet as the HistoricRallyMeter device, can view live rally data and control the meter. Multiple phones may connect at once.

Phones can:
- Receive streamed live updates of trip/total distances, current and average speeds, target speed, and the ahead/behind figure.
- Reset the trip and total distances.
- Use the **next/prev** segment correction control (same rules and behaviour as the TwinMaster `[next/prev]` button).
- View and edit the segment (stage) setup, including storing and recalling memory setups.

### Transport decision

- **No UDP.** Browsers cannot receive raw UDP datagrams, so UDP broadcast is not an option for a browser-based client. On a local subnet the latency advantage of UDP over TCP for small ~10 Hz payloads is negligible (dominated by WiFi airtime and browser render), so nothing is lost by not using it.
- **WebSocket** is used as the single bidirectional channel. Live telemetry is pushed from the device to every connected phone; control commands (reset, segment edits) travel back up the same connection. WebSocket is reliable and ordered, which is required for control operations that must not be silently dropped.
- Static assets (HTML/CSS/JS) are delivered over plain HTTP by the same embedded server.

### Server architecture

- The web server is embedded in the existing application and integrated into the GTK/GLib main loop (libsoup, which shares the `GMainContext` with GTK). This avoids a separate networking thread and, crucially, means incoming commands are handled on the same thread that owns `AppData`/`RallyState`.
- **Thread-safety rule:** all rally state (`RallyState`, including the `segments` vector, trip/total start counters, `segment_current_number`) is single-threaded and lock-free by design. Network commands MUST NOT mutate this state directly from any other thread. Every command is executed on the GLib main loop and routed through the **same functions the on-screen controls already use** (e.g. the trip/total reset handlers and the segment setters in `callbacks.cpp`), so validation, calibration recalculation, and config persistence stay identical to the physical UI. If a non-GLib server library is ever used instead, commands must be marshalled onto the main loop with `g_idle_add()`/`g_main_context_invoke()`.
- Telemetry is broadcast at a throttled **10 Hz** (decoupled from the 100 Hz internal poll loop) — ample for a human-readable phone display and light on WiFi.
- After any state change (from a phone or from the on-device screens), the current authoritative state is broadcast to all connected clients so every phone and the dash stay in sync. Conflicts between multiple editors resolve as last-writer-wins.

### Network discovery

- The server listens on a fixed TCP port (default `8080`, configurable).
- The device advertises itself over mDNS/Avahi (e.g. `historicrallymeter.local`) so phones need not know the IP address.
- The **Date/Time Setup screen** displays the connection URL as text and as a **QR code** so co-drivers can scan it with a phone camera to open the web client (see "Date/Time Setup Screen").

### Directory layout

The server-side integration and the browser client are kept in separate directories:

- `webserver/` — the C++ code that embeds the HTTP/WebSocket server, serialises telemetry to JSON, and dispatches incoming commands onto the main loop through the existing control functions. One class per file, consistent with the rest of the project.
- `webclient/` — the static web application served to and executed on the phones (`index.html`, CSS, JS, and any assets). This directory contains no device code; it is a self-contained browser client that the server publishes as static files. (A future option is to compile the C++ gauge rendering to WebAssembly and place the `.wasm` artifact here, but the initial client is plain HTML/CSS/JS with no build step.)

### Message protocol (JSON over WebSocket)

**Telemetry (device → phone), ~10 Hz:**
```json
{
  "type": "telemetry",
  "rally_clock": "14:53:07",
  "trip_m": 537,
  "total_m": 855053,
  "cur_kph": 38.0,
  "trip_avg_kph": 41.2,
  "total_avg_kph": 42.1,
  "target_kph": 100.0,
  "ahead_behind_s": -1.8,
  "segment_number": 2,
  "segment_count": 3,
  "next_prev_label": "next",
  "next_prev_enabled": true
}
```

- `next_prev_label` is one of `"next"`, `"prev"`, or `"--->"`, matching the TwinMaster button caption.
- `next_prev_enabled` is true only when the button would be active on the co-pilot display (within 500 m of the current segment end with a following segment, or within 500 m of the current segment start and not on the first segment); otherwise false and the label is `"--->"`.

**State snapshot (device → phone)** — sent on connect and after any change, so clients can render/refresh the segment editor:
```json
{
  "type": "state",
  "segment_current_number": 1,
  "segments": [
    { "target_speed_kph": 75.0, "distance_m": 1330, "autoNext": true }
  ]
}
```

**Commands (phone → device):**
```json
{ "type": "reset_trip" }
{ "type": "reset_total" }
{ "type": "next_prev" }
{ "type": "segment_set", "index": 0, "target_speed_kph": 75.0, "distance_m": 1330, "autoNext": true }
{ "type": "segment_add", "target_speed_kph": 75.0, "distance_m": 1000, "autoNext": true }
{ "type": "segment_delete", "index": 2 }
{ "type": "memory_store", "slot": 1 }
{ "type": "memory_recall", "slot": 1 }
```

- Distances are entered/displayed in metres and speeds in KPH (calibration-independent), matching the stage setup screen; the device recomputes counts against the current calibration.
- `next_prev` invokes the same logic as the TwinMaster `[next/prev]` button (`on_next_prev_segment` in `callbacks.cpp`): when within 500 m of segment end, shortens the current segment to the distance travelled and advances; when within 500 m of segment start (not the first segment), extends the previous segment and resets the current segment start. No effect if neither condition applies (same as a disabled button on the co-pilot display).
- The device validates every command and ignores malformed or out-of-range ones. Invalid entries produce no state change.

### Web client design

The client is a single responsive page that works in portrait or landscape on a phone browser, styled for legibility (large bold values, high contrast). It connects to the WebSocket on load and reconnects automatically if the connection drops. It has two views selectable by a tab bar:

**1) Live view (default)**

```
+------------------------------------------+
|  Rally Clock            14:53:07         |
+------------------------------------------+
|  AHEAD / BEHIND                          |
|            -1.8 s                         |   <- large, colour-coded
+------------------------------------------+
|  Current      38.0 kph                   |
|  Target      100.0 kph                   |
+------------------------------------------+
|  Trip     537 m     avg 41.2 kph         |
|  Total 855,053 m    avg 42.1 kph         |
+------------------------------------------+
|  Segment 2 of 3                          |
+------------------------------------------+
|  [ next ]              [ Reset Trip ]    |   <- next/prev/---> label from telemetry; greyed when disabled
+------------------------------------------+
```

- The ahead/behind value is the most prominent element and is colour-coded (e.g. green when ahead, red when behind) for a glance read.
- Distances use the same auto-formatting as the TwinMaster (metres, switching to km with a unit label for large values).
- The **next/prev** button shows the caption from `next_prev_label` (`next`, `prev`, or `--->`) and is enabled only when `next_prev_enabled` is true; it sends a `next_prev` command on press, mirroring the TwinMaster `[next/prev]` button rules (500 m window at segment end or start).
- `Reset Trip` issues a `reset_trip` command; it may require a short press-and-confirm to avoid accidental taps.

**2) Setup view**

```
+------------------------------------------+
|  # | Target kph | Distance m | Auto | x  |
|  1 |   75.0     |   1330     | [x]  |[del]|
|  2 |   75.0     |   1000     | [x]  |[del]|
|  3 |  100.0     |    250     | [ ]  |[del]|
+------------------------------------------+
|  [ + Add segment ]                       |
+------------------------------------------+
|  Memory:  [1][2][3][4][5]                |
|           [ Store ]   [ Recall ]         |
+------------------------------------------+
|  [ Reset Trip ]     [ Reset Total ]      |
```

- Each row edits one segment (target speed, distance, autoNext) and sends a `segment_set` command on change; `+ Add segment` and per-row delete send `segment_add`/`segment_delete`.
- Memory Store/Recall for the five slots mirror the on-device memory behaviour and send `memory_store`/`memory_recall`.
- The editor is populated and kept current from the `state` snapshot, so edits made on the device or on another phone appear here.

### Configuration

Two keys are added to `rally_config.json`:
- `web_enabled` (bool) — enables the embedded web server (default true).
- `web_port` (int) — TCP port to listen on (default 8080).

### Build requirements (addition)

- HTTP/WebSocket server integrated with the GLib main loop (GIO sockets; static files from `webclient/`).
- libqrencode (runtime) for QR code on the Date/Time screen (loaded dynamically).

### Security note

The server is an unauthenticated service intended for a private, in-car subnet. Because the control surface can reset distances and alter segments, it should only be exposed on a trusted network. No credentials or transport encryption are provided by default.

## Unit Tests

### I2C Counter Tests
- Read 32-bit value from CNTR_1 at address 0x70, register 0x07
- Read 32-bit value from CNTR_2 at address 0x71, register 0x07
- Verify big-endian to host byte order conversion is correct
- Verify polling returns same value if called within 5ms
- Verify polling returns new value if called after 5ms

### Config File Tests
- Load config from valid JSON file with all fields present
- Load config from JSON file with missing fields (defaults applied)
- Load config when file does not exist (all defaults applied)
- Load config with segments array containing multiple segments
- Load config with empty segments array
- Save config and verify all fields written correctly
- Save config with multiple segments and verify JSON structure
- Segment target_speed_counts_per_hour and distance_counts saved as double with 6 decimal places
- Verify calibration defaults to 600000 when missing
- Verify units defaults to false (KPH) when missing
- Verify segment_current_number defaults to -1 when missing/empty segments

### Distance Calculation Tests
- Single counter mode: distance = CNTR_1 - start_cntr1
- Dual counter mode: distance = ((CNTR_1 - start_cntr1) + (CNTR_2 - start_cntr2)) / 2
- Verify integer math (no floating point until final display)
- Distance in meters = (count_diff * calibration) / 1000 / 1000
- Distance in centimetres = (count_diff * calibration) / 10000
- Handle 32-bit counter wrap-around correctly

### Calibration Tests
- Default calibration value is 600000
- New calibration = (input_meters * 1000 * 1000) / total_count_diff
- Minimum input distance is 500 meters
- Maximum input distance is 100,000 meters
- Calibration change does not affect stored segment target speeds
- Calibration change affects all subsequent distance/speed calculations

### Speed Calculation Tests
- Speed in counts/hour from counts and time_ms
- Convert counts/hour to KPH: (counts_per_hour * calibration) / 1e9 (high precision double)
- Convert counts/hour to MPH: KPH * 100000 / 160934
- kphToCountsPerHour() returns double for high precision
- countsToMeters() returns double for high precision distance calculations
- 100 KPH displays as "62.14" MPH after unit switch
- Average speed since Total reset
- Average speed since Trip reset
- Average speed since Segment start
- Speed displays "--.--" when time elapsed is zero

### Current Speed Rolling Average Tests
- First poll stores value in array position 0
- Array shifts down when >200ms since position 0 timestamp
- Array does not shift if <200ms since position 0 timestamp
- 10th position (index 9) available for speed calculation
- get10th() returns {0,0,0} when position 9 time is zero
- get10th() returns {0,0,0} when age < 1280ms (80% of 2s with 20% tolerance)
- get10th() returns valid data when age >= 1280ms
- getMostRecent() returns actual latest I2C read, not array position 0
- Current speed = (most_recent - 10th) counts / time difference
- Display "--.--" when 10th position is invalid

### Segment Management Tests
- Add segment with target_speed, distance, autoNext
- Delete segment from list
- Segment target_speed stored as counts per hour (double, high precision)
- Segment distance stored as counts (double, high precision)
- Config file saves segment values with 6 decimal places
- Counts per hour = kphToCountsPerHour(input_kph, calibration) - returns double
- AutoNext=true advances segment when distance reached
- AutoNext=false requires manual next segment button
- Skip multiple segments if polling interval causes overshoot
- No current segment (index -1) shows "--.--" for Seg speed
- Past end of last segment by >1000m shows "--.--" for Seg speed

### Ahead/Behind Calculation Tests
- All calculations use high precision (double) floating point
- ideal_counts = (time_ms_since_segment / 3600000.0) * target_counts_per_hour
- diff = actual_counts - ideal_counts
- seconds = diff / (target_counts_per_hour / 3600.0)
- Positive seconds means travelling too fast (ahead)
- Negative seconds means travelling too slow (behind)
- Display "+xxxxx" for ahead, "-xxxxx" for behind

### ETA Calculation Tests
- All calculations use high precision (double) floating point
- ETA = remaining_segment_distance / current_speed
- remaining_segment_distance calculated in meters using countsToMeters()
- Display "--.--" when current speed is zero
- Display "Over by hh:mm:ss" when past segment end (negative remaining)
- Format ETA as hh:mm:ss

### RallyClock Tests
- RallyClock = system_time + rallyTimeOffset_ms
- Default rallyTimeOffset_ms is 0
- Set and save updates rallyTimeOffset_ms correctly
- RallyClock displays in 24-hour format (hh:mm:ss)

### Reset Functionality Tests
- Total reset sets total_start_cntr1 = current CNTR_1
- Total reset sets total_start_cntr2 = current CNTR_2
- Total reset sets total_start_time_ms = current time
- Trip reset sets trip_start_cntr1/cntr2 and trip_start_time_ms
- Next segment resets Trip counters and time
- Next segment increments segment_current_number
- Next segment sets segment_start counters and time

### Unit Toggle Tests
- Toggle units from KPH to MPH
- Toggle units from MPH to KPH
- All speed displays update on next refresh after toggle
- Header row shows current unit selection

### Display Update Tests
- Updates per second counts render calls in last full second
- Rolling count resets each second
- Driver display updates all speed values each refresh
- Co-pilot TwinMaster updates distance and time values

### Time Formatting Tests
- Format milliseconds as hh:mm:ss
- Format milliseconds as hhh:mm:ss for durations over 99 hours
- All times display in 24-hour format
- Rally clock displays current time with offset applied

### Stage Setup Screen Tests
- Input target speed in KPH (decimal allowed) and verify stored as counts per hour (double)
- Input distance in meters (decimal allowed) and verify stored as counts (double)
- Toggle autoNext Y/N and verify boolean stored correctly
- Add new segment to end of list
- Delete segment from middle of list
- Delete last segment from list
- Verify segment list displays all segments with correct values
- Back button returns to TwinMaster without losing unsaved changes

### Calibration Screen Tests
- Display total distance in meters using current calibration
- Display total count difference (raw counter value)
- Input validation rejects values below 500 meters
- Input validation rejects values above 100,000 meters
- Save button calculates and stores new calibration
- Back button returns without saving changes
- Verify calibration update does not modify existing segment target speeds

### TwinMaster Screen Tests
- Display Total distance in meters with time elapsed (hhh:mm:ss ago)
- Display Trip distance in meters with time elapsed (hhh:mm:ss ago)
- Display current segment number and distance to segment end
- Display negative distance when past segment end
- Total reset button updates counters and time, saves to JSON
- Trip reset button updates counters and time, saves to JSON
- Segments button navigates to Stage Setup screen
- Next segment button advances segment and resets Trip
- Calibration button navigates to Calibration screen
- RallyClock displays at top in hh:mm:ss format

### Date/Time Setup Screen Tests
- Display system clock in yyyy/mm/dd hh:mm:ss format
- Display RallyClock (with current offset) in yyyy/mm/dd hh:mm:ss format
- When `web_enabled` is true, display web client URL and QR code encoding that URL; omit both when `web_enabled` is false
- QR code resolves to a page loadable on a phone on the same subnet
- Input fields accept valid date and time values
- Calculate rallyTimeOffset = input_rally_time_ms - system_time_ms
- Set and save button stores offset and returns to TwinMaster
- Back button returns without saving changes

### Remote Web Access Tests
- WebSocket telemetry includes `next_prev_label` and `next_prev_enabled` consistent with TwinMaster button state
- `next_prev` command applies same segment correction as co-pilot `[next/prev]` button; no-op when not in 500 m window
- Multiple connected phones receive broadcast state after `next_prev`, reset, or segment edit

### Rally-Specific Edge Cases
- Zero counts (stationary vehicle) - speed displays as 0.00 or "--.--"
- High speeds (200+ KPH) calculated and displayed correctly
- Long stages (hundreds of kilometres) without precision loss (high precision double throughout)
- Short segments (2km minimum) handled correctly
- Timing accuracy to 0.1 seconds for ahead/behind calculation (high precision)
- Rapid segment transitions when autoNext enabled at high speed

### Error Handling Tests
- I2C read failure returns previous valid value or error state
- Invalid JSON field types use default values
- Negative distance values rejected or handled gracefully
- Negative speed values display as 0.00
- Empty segment array during active rally (segment_current_number >= 0)
- System time jumps forward handled (recalculate elapsed times)
- System time jumps backward handled (prevent negative durations)
- Counter overflow at 32-bit boundary (wrap-around handling)

