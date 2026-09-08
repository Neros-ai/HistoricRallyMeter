#include <iostream>
#include <exception>
#include <system_error>
#include <cstring>
#include <cstdint>
#include <string>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <gtk/gtk.h>
#include "i2c_counter.h"
#include "i_counter.h"
#include "sim_counter.h"
#include <memory>
#include <cstdlib>
#include "rally_state.h"
#include "config_file.h"
#include "counter_poller.h"
#include "rally_types.h"
#include "ui_driver.h"
#include "ui_copilot.h"
#include "ui_control.h"
#include "callbacks.h"
#include "webserver/rally_web_server.h"
#include "calculations.h"
#include "tone_generator.h"

// Structure to hold connector info from DRM
struct DrmConnector {
    std::string name;       // e.g., "DSI-2", "HDMI-A-1"
    std::string status;     // "connected" or "disconnected"
    int width = 0;
    int height = 0;
};

// Read connector information from /sys/class/drm/
static std::vector<DrmConnector> getDrmConnectors() {
    std::vector<DrmConnector> connectors;
    const std::string drm_path = "/sys/class/drm/";
    
    try {
        for (const auto& entry : std::filesystem::directory_iterator(drm_path)) {
            std::string name = entry.path().filename().string();
            
            // Skip card entries, look for connector entries like "card1-DSI-2"
            if (name.find("card") == 0 && name.find('-') != std::string::npos) {
                DrmConnector conn;
                
                // Extract connector name (e.g., "DSI-2" from "card1-DSI-2")
                size_t dash_pos = name.find('-');
                if (dash_pos != std::string::npos) {
                    conn.name = name.substr(dash_pos + 1);
                }
                
                // Read status
                std::ifstream status_file(entry.path() / "status");
                if (status_file) {
                    std::getline(status_file, conn.status);
                }
                
                // Read modes to get resolution
                std::ifstream modes_file(entry.path() / "modes");
                if (modes_file) {
                    std::string mode;
                    if (std::getline(modes_file, mode)) {
                        // Parse mode like "1280x400" or "3840x2160"
                        size_t x_pos = mode.find('x');
                        if (x_pos != std::string::npos) {
                            try {
                                conn.width = std::stoi(mode.substr(0, x_pos));
                                conn.height = std::stoi(mode.substr(x_pos + 1));
                            } catch (...) {}
                        }
                    }
                }
                
                if (!conn.name.empty()) {
                    connectors.push_back(conn);
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Warning: Could not read DRM connectors: " << e.what() << std::endl;
    }
    
    return connectors;
}

// Connector sort priority: DSI before HDMI, then by number (lowest first)
static int connectorPriority(const std::string& name) {
    std::string lower = name;
    for (auto& c : lower) c = std::tolower(c);

    int type_priority = 2;  // unknown types last
    if (lower.find("dsi") != std::string::npos) type_priority = 0;
    else if (lower.find("hdmi") != std::string::npos) type_priority = 1;

    int number = 99;
    for (size_t i = 0; i < lower.size(); i++) {
        if (std::isdigit(lower[i])) {
            number = std::stoi(lower.substr(i));
            break;
        }
    }
    return type_priority * 100 + number;
}

// Co-pilot-capable display: exactly 1280x400 (or rotated)
static bool is1280x400(int w, int h) {
    return (w == 1280 && h == 400) || (w == 400 && h == 1280);
}

static bool is800x480(int w, int h) {
    return (w == 800 && h == 480) || (w == 480 && h == 800);
}

// Small display: suitable for either window (driver also accepts 800x480)
static bool isSmallDisplay(int w, int h) {
    return is1280x400(w, h) || is800x480(w, h);
}

struct DisplayMatch {
    GdkMonitor* monitor;
    int monitor_index;
    std::string connector_name;
    int priority;          // lower = assigned first
    bool copilot_capable;  // true only for 1280x400 panels
};

// Find all small monitors (1280x400 / 800x480, incl. rotated), matched to DRM
// connectors where possible, sorted by connector priority (DSI before HDMI,
// then by connector number; unmatched monitors sort last)
static std::vector<DisplayMatch> findSmallDisplays(GdkDisplay* display) {
    int n_monitors = gdk_display_get_n_monitors(display);
    auto connectors = getDrmConnectors();

    std::cout << "System connectors:" << std::endl;
    for (const auto& conn : connectors) {
        std::cout << "  " << conn.name << ": " << conn.status
                  << " (" << conn.width << "x" << conn.height << ")" << std::endl;
    }

    std::cout << "GDK monitors (" << n_monitors << "):" << std::endl;

    std::vector<DisplayMatch> matches;

    for (int i = 0; i < n_monitors; i++) {
        GdkMonitor* monitor = gdk_display_get_monitor(display, i);
        GdkRectangle geometry;
        gdk_monitor_get_geometry(monitor, &geometry);

        const char* model = gdk_monitor_get_model(monitor);
        const char* manufacturer = gdk_monitor_get_manufacturer(monitor);

        std::cout << "  Monitor " << i << ": "
                  << geometry.width << "x" << geometry.height
                  << " at (" << geometry.x << "," << geometry.y << ")"
                  << " model=" << (model ? model : "null")
                  << " mfr=" << (manufacturer ? manufacturer : "null")
                  << std::endl;

        if (!isSmallDisplay(geometry.width, geometry.height))
            continue;

        DisplayMatch dm;
        dm.monitor = monitor;
        dm.monitor_index = i;
        dm.connector_name = "";
        dm.priority = 999;
        dm.copilot_capable = is1280x400(geometry.width, geometry.height);

        for (auto& conn : connectors) {
            if (conn.status != "connected") continue;
            bool res_match = (geometry.width == conn.width && geometry.height == conn.height) ||
                             (geometry.width == conn.height && geometry.height == conn.width);
            if (res_match) {
                int pri = connectorPriority(conn.name);
                if (pri < dm.priority) {
                    dm.priority = pri;
                    dm.connector_name = conn.name;
                }
            }
        }

        std::cout << "  -> small display matched ("
                  << geometry.width << "x" << geometry.height
                  << (dm.copilot_capable ? ", co-pilot capable" : ", driver only");
        if (!dm.connector_name.empty())
            std::cout << ", connector: " << dm.connector_name << ", priority " << dm.priority;
        std::cout << ")" << std::endl;

        matches.push_back(dm);
    }

    std::stable_sort(matches.begin(), matches.end(),
              [](const DisplayMatch& a, const DisplayMatch& b) { return a.priority < b.priority; });

    return matches;
}

// Save driver window position callback
static gboolean on_driver_configure(GtkWidget* widget, GdkEventConfigure* event, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    
    // Get size from event or window
    if (event) {
        data->state->driver_window_width = event->width;
        data->state->driver_window_height = event->height;
    } else {
        gint width, height;
        gtk_window_get_size(GTK_WINDOW(widget), &width, &height);
        data->state->driver_window_width = width;
        data->state->driver_window_height = height;
    }
    
    // Get monitor and calculate position
    GdkDisplay* display = gdk_display_get_default();
    GdkWindow* gdk_window = gtk_widget_get_window(widget);
    if (gdk_window) {
        GdkMonitor* monitor = gdk_display_get_monitor_at_window(display, gdk_window);
        int n_monitors = gdk_display_get_n_monitors(display);
        for (int i = 0; i < n_monitors; i++) {
            if (gdk_display_get_monitor(display, i) == monitor) {
                data->state->driver_window_monitor = i;
                
                // Try multiple methods to get window position
                gint root_x = 0, root_y = 0;
                
                // Method 1: gdk_window_get_root_origin (X11)
                gdk_window_get_root_origin(gdk_window, &root_x, &root_y);
                
                // Method 2: If that returns 0,0 try gdk_window_get_origin
                if (root_x == 0 && root_y == 0) {
                    gdk_window_get_origin(gdk_window, &root_x, &root_y);
                }
                
                // Method 3: Use event x,y if available (may work on some Wayland compositors)
                if (root_x == 0 && root_y == 0 && event) {
                    root_x = event->x;
                    root_y = event->y;
                }
                
                // Store position relative to monitor origin
                GdkRectangle mon_geom;
                gdk_monitor_get_geometry(monitor, &mon_geom);
                data->state->driver_window_x = root_x - mon_geom.x;
                data->state->driver_window_y = root_y - mon_geom.y;
                
                break;
            }
        }
    }
    
    return FALSE;  // Continue processing
}

// Save state on window close
static gboolean on_window_close_save(G_GNUC_UNUSED GtkWidget* widget, G_GNUC_UNUSED GdkEvent* event, gpointer user_data) {
    AppData* data = static_cast<AppData*>(user_data);
    
    // Position/size should already be tracked via configure-event
    // Just save the current state to config file
    std::cout << "Saving window - relative position: (" << data->state->driver_window_x 
              << "," << data->state->driver_window_y << ") "
              << "size: " << data->state->driver_window_width << "x" << data->state->driver_window_height
              << " monitor: " << data->state->driver_window_monitor << std::endl;
    
    ConfigFile::save(*data->state);
    
    gtk_main_quit();
    return TRUE;
}

static ToneGenerator* g_beepToneGen = nullptr;

static gboolean on_button_beep(GSignalInvocationHint*, guint n_param_values,
                                const GValue* param_values, gpointer) {
    if (!g_beepToneGen) return TRUE;
    // param_values[0] is the emitting instance (the clicked button). Skip
    // the click-feedback beep for buttons marked "no_click_beep" -- the
    // Sim Control Panel's speed/START/STOP buttons, which fire dozens of
    // times during a test drive and don't want a beep on every press.
    if (n_param_values > 0) {
        GObject* instance = static_cast<GObject*>(g_value_get_object(&param_values[0]));
        if (instance && g_object_get_data(instance, "no_click_beep")) {
            return TRUE;
        }
    }
    g_beepToneGen->playBeep();
    return TRUE;
}

// Counts/sec for the simulated counter when RALLY_SIM_I2C=1 (off-target dev).
// ~1000 c/s stays well under CounterPoller's per-poll jump guard (1000 counts
// per ~5-10ms poll), so simulated reads are never rejected as spurious.
static constexpr double kSimCountsPerSecond = 1000.0;

// Build the counter backend: a real I2C counter on hardware, or a simulated
// counter when RALLY_SIM_I2C=1 so the GUI can start on a dev machine with no
// /dev/i2c-1. Real hardware is the default, so the Pi target is unaffected.
// Single source of truth for "are we simulating?". The log line below used to
// test only for the variable's presence while this tested its value, so
// RALLY_SIM_I2C=0 announced simulation while real I2C was used, and
// RALLY_SIM_I2C=true announced it and then threw opening /dev/i2c-1.
static bool usingSimCounters() {
    const char* sim = std::getenv("RALLY_SIM_I2C");
    return sim && std::string(sim) == "1";
}

static std::unique_ptr<ICounter> makeCounter(int bus, int address) {
    if (usingSimCounters()) {
        return std::make_unique<SimCounter>(0u, kSimCountsPerSecond);
    }
    return std::make_unique<I2CCounter>(bus, address);
}

int main(int argc, char* argv[]) {
    try {
        const int I2C_BUS = 1;
        const int CNTR_1_ADDRESS = 0x70;
        const int CNTR_2_ADDRESS = 0x71;
        const uint8_t REGISTER = 0x07;
        
        std::cerr << "[DEBUG] Step 1: calling gtk_init..." << std::endl;
        gtk_init(&argc, &argv);
        std::cerr << "[DEBUG] Step 1: gtk_init OK" << std::endl;
        
        // Force dark theme for both windows
        GtkSettings* settings = gtk_settings_get_default();
        g_object_set(settings,
                     "gtk-theme-name", "Adwaita-dark",
                     "gtk-application-prefer-dark-theme", TRUE,
                     NULL);
        std::cerr << "[DEBUG] Step 2: GTK theme set OK" << std::endl;
        
        // Load state
        RallyState state;
        ConfigFile::load(state);
        std::cerr << "[DEBUG] Step 3: Config loaded OK" << std::endl;
        
        // Initialize counters from current values if not set
        std::cerr << "[DEBUG] Step 4: Opening I2C counter1 at 0x70..." << std::endl;
        std::unique_ptr<ICounter> counter1 = makeCounter(I2C_BUS, CNTR_1_ADDRESS);
        std::cerr << "[DEBUG] Step 4: counter1 OK" << std::endl;
        std::cerr << "[DEBUG] Step 5: Opening I2C counter2 at 0x71..." << std::endl;
        std::unique_ptr<ICounter> counter2 = makeCounter(I2C_BUS, CNTR_2_ADDRESS);
        std::cerr << "[DEBUG] Step 5: counter2 OK" << std::endl;
        if (usingSimCounters()) {
            std::cerr << "[DEBUG] RALLY_SIM_I2C=1: using simulated counters" << std::endl;
        }
        
        if (state.total_start_cntr1 == 0 && state.total_start_cntr2 == 0) {
            state.total_start_cntr1 = counter1->readRegister(REGISTER);
            state.total_start_cntr2 = counter2->readRegister(REGISTER);
        }
        if (state.trip_start_cntr1 == 0 && state.trip_start_cntr2 == 0) {
            state.trip_start_cntr1 = counter1->readRegister(REGISTER);
            state.trip_start_cntr2 = counter2->readRegister(REGISTER);
        }
        if (state.segment_start_cntr1 == 0 && state.segment_start_cntr2 == 0) {
            state.segment_start_cntr1 = counter1->readRegister(REGISTER);
            state.segment_start_cntr2 = counter2->readRegister(REGISTER);
        }
        
        // Create application data
        std::cerr << "[DEBUG] Step 6: Creating AppData..." << std::endl;
        AppData app_data;
        app_data.counter1 = counter1.get();
        app_data.counter2 = counter2.get();
        app_data.register_addr = REGISTER;
        // Non-null only when RALLY_SIM_I2C=1 (real I2CCounter does not derive
        // from SimCounter, so this is nullptr on real hardware).
        app_data.simCounter1 = dynamic_cast<SimCounter*>(counter1.get());
        app_data.simCounter2 = dynamic_cast<SimCounter*>(counter2.get());
        // Sim Control Panel starts stopped, not running at whatever speed
        // the last session left it -- an operator opening a fresh sandbox
        // should see a parked vehicle, not one already in motion.
        if (app_data.simCounter1) app_data.simCounter1->setPaused(true);
        if (app_data.simCounter2) app_data.simCounter2->setPaused(true);
        app_data.state = &state;
        app_data.poller = new CounterPoller();
        std::cerr << "[DEBUG] Step 7: CounterPoller created OK" << std::endl;
        std::cerr << "[DEBUG] Step 8: Creating ToneGenerator..." << std::endl;
        app_data.toneGen = new ToneGenerator();
        std::cerr << "[DEBUG] Step 8a: ToneGenerator constructed OK" << std::endl;
        std::cerr << "[DEBUG] Step 8b: Calling toneGen->start()..." << std::endl;
        app_data.toneGen->start();
        std::cerr << "[DEBUG] Step 8c: ToneGenerator started OK" << std::endl;
        app_data.lastUpdateCountTime_ms = getRallyTime_ms(state);

        if (state.web_enabled) {
            app_data.webServer = new RallyWebServer(&app_data);
            if (app_data.webServer->start(state.web_port)) {
                std::cout << "Web server listening on " << app_data.webServer->getWebUrl() << std::endl;
            } else {
                std::cerr << "Web server failed to start on port " << state.web_port << std::endl;
            }
        }

        // Single-display mode: exactly one monitor and it is 1280x400, or the
        // "force single display mode" option is set in the config.
        // Only the co-pilot window is shown; the compact driver display is
        // embedded in the TwinMaster right panel instead of the alarm panel.
        // Must be decided before the windows are created.
        {
            bool single = state.force_single_display;
            if (single) {
                std::cout << "Single-display mode forced by config option" << std::endl;
            } else {
                GdkDisplay* d = gdk_display_get_default();
                if (gdk_display_get_n_monitors(d) == 1) {
                    GdkRectangle geom;
                    gdk_monitor_get_geometry(gdk_display_get_monitor(d, 0), &geom);
                    single = is1280x400(geom.width, geom.height);
                }
            }
            if (single) {
                app_data.singleDisplayMode = true;
                app_data.driverCompactMode = true;
                std::cout << "Single-display mode: co-pilot window only "
                          << "with embedded driver gauge" << std::endl;
            }
        }

        // Create windows
        std::cerr << "[DEBUG] Step 9: Creating driver window..." << std::endl;
        app_data.driverWindow = createDriverWindow(&app_data);
        std::cerr << "[DEBUG] Step 9: Driver window created OK" << std::endl;
        std::cerr << "[DEBUG] Step 10: Creating copilot window..." << std::endl;
        app_data.copilotWindow = createCopilotWindow(&app_data);
        std::cerr << "[DEBUG] Step 10: Copilot window created OK" << std::endl;

        // Sim Control Panel: a 3rd window for off-target testing only.
        // Only created when the simulated sensor feed is active.
        if (app_data.simCounter1 || app_data.simCounter2) {
            std::cerr << "[DEBUG] Step 10a: Creating control panel window..." << std::endl;
            app_data.controlWindow = createControlWindow(&app_data);
            std::cerr << "[DEBUG] Step 10a: Control panel window created OK" << std::endl;
        }

        // Install global button-click beep
        g_beepToneGen = app_data.toneGen;
        guint clicked_id = g_signal_lookup("clicked", GTK_TYPE_BUTTON);
        if (clicked_id > 0) {
            g_signal_add_emission_hook(clicked_id, 0, on_button_beep, nullptr, nullptr);
        }
        
        // Find small displays sorted by priority (DSI first, then HDMI, lowest number first)
        GdkDisplay* display = gdk_display_get_default();
        auto small_displays = findSmallDisplays(display);

        std::cout << "Found " << small_displays.size() << " small display(s)" << std::endl;

        GdkMonitor* copilot_monitor = nullptr;
        int copilot_index = -1;
        GdkMonitor* driver_monitor = nullptr;
        int driver_monitor_index = -1;

        // Co-pilot takes the highest-priority co-pilot-capable (1280x400) display
        size_t copilot_slot = SIZE_MAX;
        for (size_t s = 0; s < small_displays.size(); s++) {
            if (small_displays[s].copilot_capable) {
                copilot_slot = s;
                break;
            }
        }
        if (copilot_slot != SIZE_MAX) {
            copilot_monitor = small_displays[copilot_slot].monitor;
            copilot_index = small_displays[copilot_slot].monitor_index;
            std::cout << "Co-pilot assigned to monitor " << copilot_index;
            if (!small_displays[copilot_slot].connector_name.empty())
                std::cout << " (" << small_displays[copilot_slot].connector_name << ")";
            std::cout << std::endl;
        }

        // Driver takes the highest-priority remaining small display (1280x400 or 800x480)
        size_t driver_slot = SIZE_MAX;
        for (size_t s = 0; s < small_displays.size(); s++) {
            if (s != copilot_slot) {
                driver_slot = s;
                break;
            }
        }
        bool driver_fullscreen = (driver_slot != SIZE_MAX);
        if (driver_fullscreen) {
            driver_monitor = small_displays[driver_slot].monitor;
            driver_monitor_index = small_displays[driver_slot].monitor_index;
            std::cout << "Driver assigned to monitor " << driver_monitor_index;
            if (!small_displays[driver_slot].connector_name.empty())
                std::cout << " (" << small_displays[driver_slot].connector_name << ")";
            std::cout << std::endl;
        }

        // Position co-pilot window
        if (copilot_monitor) {
            GdkRectangle geometry;
            gdk_monitor_get_geometry(copilot_monitor, &geometry);
            std::cout << "Positioning co-pilot window on monitor " << copilot_index
                      << " at (" << geometry.x << "," << geometry.y << ")" << std::endl;

            gtk_window_set_default_size(GTK_WINDOW(app_data.copilotWindow),
                                        geometry.width, geometry.height);
            gtk_window_move(GTK_WINDOW(app_data.copilotWindow), geometry.x, geometry.y);
        } else {
            std::cout << "No co-pilot-capable (1280x400) display found, opening co-pilot as 1280x400 window" << std::endl;
            gtk_window_set_default_size(GTK_WINDOW(app_data.copilotWindow), 1280, 400);
            gtk_window_resize(GTK_WINDOW(app_data.copilotWindow), 1280, 400);
        }

        // Position driver window (skipped entirely in single-display mode)
        if (app_data.singleDisplayMode) {
            std::cout << "Single-display mode: driver window not shown" << std::endl;
        } else if (driver_fullscreen) {
            // A small display remains for the driver: fullscreen on it
            GdkRectangle mon_geometry;
            gdk_monitor_get_geometry(driver_monitor, &mon_geometry);
            std::cout << "Driver fullscreen on small display, monitor " << driver_monitor_index
                      << " at (" << mon_geometry.x << "," << mon_geometry.y << ")" << std::endl;
            gtk_window_set_default_size(GTK_WINDOW(app_data.driverWindow),
                                        mon_geometry.width, mon_geometry.height);
            gtk_window_move(GTK_WINDOW(app_data.driverWindow), mon_geometry.x, mon_geometry.y);
        } else {
            // No small display remains: restore saved size and monitor
            std::cout << "Restoring driver window: size "
                      << state.driver_window_width << "x" << state.driver_window_height
                      << " on monitor " << state.driver_window_monitor << std::endl;

            gtk_window_set_default_size(GTK_WINDOW(app_data.driverWindow),
                                        state.driver_window_width, state.driver_window_height);

            int n_monitors = gdk_display_get_n_monitors(display);

            if (state.driver_window_monitor >= 0 && state.driver_window_monitor < n_monitors) {
                GdkMonitor* saved_monitor = gdk_display_get_monitor(display, state.driver_window_monitor);
                if (saved_monitor && saved_monitor != copilot_monitor) {
                    driver_monitor = saved_monitor;
                    driver_monitor_index = state.driver_window_monitor;
                }
            }

            if (!driver_monitor) {
                for (int i = 0; i < n_monitors; i++) {
                    GdkMonitor* mon = gdk_display_get_monitor(display, i);
                    if (mon != copilot_monitor) {
                        driver_monitor = mon;
                        driver_monitor_index = i;
                        break;
                    }
                }
            }

            if (driver_monitor) {
                GdkRectangle mon_geometry;
                gdk_monitor_get_geometry(driver_monitor, &mon_geometry);

                std::cout << "Driver monitor " << driver_monitor_index
                          << " geometry: " << mon_geometry.width << "x" << mon_geometry.height
                          << " at (" << mon_geometry.x << "," << mon_geometry.y << ")" << std::endl;

                int abs_x, abs_y;
                bool position_valid = !(state.driver_window_x == 0 && state.driver_window_y == 0) &&
                                      state.driver_window_x >= 0 && state.driver_window_y >= 0 &&
                                      state.driver_window_x < mon_geometry.width &&
                                      state.driver_window_y < mon_geometry.height;

                if (position_valid) {
                    abs_x = mon_geometry.x + state.driver_window_x;
                    abs_y = mon_geometry.y + state.driver_window_y;
                    std::cout << "Using saved position" << std::endl;
                } else {
                    abs_x = mon_geometry.x + (mon_geometry.width - state.driver_window_width) / 2;
                    abs_y = mon_geometry.y + (mon_geometry.height - state.driver_window_height) / 2;
                    std::cout << "Centering window (saved position invalid)" << std::endl;
                }

                std::cout << "Positioning driver window at (" << abs_x << "," << abs_y << ")" << std::endl;
                gtk_window_move(GTK_WINDOW(app_data.driverWindow), abs_x, abs_y);
            }
        }

        // Connect window delete handlers (save state on close)
        g_signal_connect(app_data.driverWindow, "delete-event",
                         G_CALLBACK(on_window_close_save), &app_data);
        g_signal_connect(app_data.copilotWindow, "delete-event",
                         G_CALLBACK(on_window_close_save), &app_data);

        // Track driver window position changes (only useful in windowed fallback mode)
        if (!driver_fullscreen && !app_data.singleDisplayMode) {
            g_signal_connect(app_data.driverWindow, "configure-event",
                             G_CALLBACK(on_driver_configure), &app_data);
        }

        // Connect button handlers
        g_signal_connect(app_data.unitToggleBtn, "clicked", G_CALLBACK(on_unit_toggle), &app_data);

        // Show windows (driver window stays hidden in single-display mode)
        std::cerr << "[DEBUG] Step 11: Showing windows..." << std::endl;
        if (!app_data.singleDisplayMode) {
            gtk_widget_show_all(app_data.driverWindow);
            std::cerr << "[DEBUG] Step 11a: Driver window shown" << std::endl;
        }
        gtk_widget_show_all(app_data.copilotWindow);
        std::cerr << "[DEBUG] Step 11b: Copilot window shown" << std::endl;

        if (app_data.controlWindow) {
            gtk_widget_show_all(app_data.controlWindow);
            std::cerr << "[DEBUG] Step 11c: Control panel window shown" << std::endl;
        }

        // Fullscreen co-pilot on its monitor AFTER showing (required for Wayland)
        if (copilot_monitor && copilot_index >= 0) {
            std::cout << "Fullscreening co-pilot on monitor " << copilot_index << std::endl;
            gtk_window_fullscreen_on_monitor(GTK_WINDOW(app_data.copilotWindow),
                gdk_display_get_default_screen(display), copilot_index);
        }

        // Fullscreen driver when it has its own small display
        if (driver_fullscreen && driver_monitor_index >= 0) {
            std::cout << "Fullscreening driver on monitor " << driver_monitor_index << std::endl;
            gtk_window_fullscreen_on_monitor(GTK_WINDOW(app_data.driverWindow),
                gdk_display_get_default_screen(display), driver_monitor_index);
        }
        
        // Set up timer (10ms = 100Hz)
        std::cerr << "[DEBUG] Step 12: Setting up 10ms timer and entering gtk_main..." << std::endl;
        g_timeout_add(10, update_display, &app_data);
        
        gtk_main();
        std::cerr << "[DEBUG] gtk_main returned normally" << std::endl;
        
        if (app_data.toneGen) {
            app_data.toneGen->stop();
            delete app_data.toneGen;
        }
        if (app_data.webServer) {
            app_data.webServer->stop();
            delete app_data.webServer;
            app_data.webServer = nullptr;
        }
        delete app_data.poller;
        return 0;
    } catch (const std::system_error& e) {
        std::cerr << "[DEBUG CRASH] std::system_error: " << e.what() 
                  << " (code: " << e.code() << " = " << e.code().message() << ")" << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "[DEBUG CRASH] std::exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "[DEBUG CRASH] Unknown exception caught" << std::endl;
        return 1;
    }
}
