#ifndef NERO_FRAME_H
#define NERO_FRAME_H

// Nero2.0 framework: the menu page, the menu key and the one shared keypad.
// Geometry and key tables are plain constants so the unit suite can check the
// sums; the GTK builders sit below under #ifndef RALLY_NO_GTK.

#include <cstring>

namespace nero {

// The release name shown on Settings, the Setup page and the phone's Set tab.
constexpr const char* VERSION = "Nero2.01";

// Usable area of the fixed 1280x400 co-pilot panel (1px border, 5px padding).
constexpr int PANEL_W = 1268;
constexpr int PANEL_H = 388;

// Shared keypad: 3 columns x 5 rows.
constexpr int KEYPAD_KEY_W = 76;
constexpr int KEYPAD_KEY_H = 52;
constexpr int KEYPAD_GAP = 6;
constexpr int KEYPAD_COLS = 3;
constexpr int KEYPAD_ROWS = 5;
constexpr int KEYPAD_COL_W = 240;
constexpr int KEYPAD_H = 284;
// Clear space between the keypad and the menu key beneath it.
constexpr int KEYPAD_MENU_GAP = 34;

constexpr int MENU_KEY_W = 130;
constexpr int MENU_KEY_H = 70;

// Menu page: 3 x 2 grid of keys filling the panel.
constexpr int MENU_COLS = 3;
constexpr int MENU_ROWS = 2;
constexpr int MENU_GAP = 12;

// Row-major. "C" stays capital; the last key is backspace.
constexpr const char* KEYPAD_LABELS[KEYPAD_COLS * KEYPAD_ROWS] = {
    "1", "2", "3",
    "4", "5", "6",
    "7", "8", "9",
    ":", "0", ".",
    "C", ";", "\xE2\x86\x90",  // U+2190 LEFTWARDS ARROW
};

struct MenuItem {
    const char* label;
    const char* caption;
    const char* page;  // GtkStack child name; nullptr = exit the app
};

constexpr MenuItem MENU_ITEMS[6] = {
    {"navigation",  "main page",                  "twinmaster"},
    {"segments",    "stage setup",                "stagesetup"},
    {"date / time", "rally time",                 "datetime"},
    {"calibration", "pulses / km",                "calibration"},
    {"settings",    "beep, tone, units, Wi-Fi",   "settings"},
    {"exit",        "close the app",              nullptr},
};

inline int menuKeyWidth()  { return (PANEL_W - (MENU_COLS - 1) * MENU_GAP) / MENU_COLS; }
inline int menuKeyHeight() { return (PANEL_H - (MENU_ROWS - 1) * MENU_GAP) / MENU_ROWS; }

inline bool isBackspaceLabel(const char* label) {
    return std::strcmp(label, KEYPAD_LABELS[KEYPAD_COLS * KEYPAD_ROWS - 1]) == 0;
}

}  // namespace nero

#ifndef RALLY_NO_GTK
#include <gtk/gtk.h>
#include "rally_types.h"

// 76x52 keys from nero::KEYPAD_LABELS. Digits, ':' and ';' go to
// on_keypad_digit, "C" to on_keypad_clear, the arrow to on_keypad_backspace.
GtkWidget* createNeroKeypad(AppData* data);

// The 130x70 "menu" key; opens the menu page.
GtkWidget* createMenuKey(AppData* data);

// The full-screen menu page: nero::MENU_ITEMS in a 3 x 2 grid.
GtkWidget* createMenuScreen(AppData* data);
#endif

#endif // NERO_FRAME_H
