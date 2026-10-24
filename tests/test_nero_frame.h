#ifndef TEST_NERO_FRAME_H
#define TEST_NERO_FRAME_H

#include "test_framework.h"
#include "../nero_frame.h"
#include <cctype>
#include <string>

// Geometry and tables for the Nero2.0 framework (menu page, menu key, shared
// keypad). The same sums are written out in tools/layout-preview/nero2/*.html;
// if a mockup moves, these expectations move with it.
class TestNeroFrame {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Nero Frame");

        suite->addTest("keypad column is 240 wide", []() {
            using namespace nero;
            return KEYPAD_COLS * KEYPAD_KEY_W + (KEYPAD_COLS - 1) * KEYPAD_GAP == KEYPAD_COL_W
                && KEYPAD_COL_W == 240;
        });

        suite->addTest("keypad is 284 high", []() {
            using namespace nero;
            return KEYPAD_ROWS * KEYPAD_KEY_H + (KEYPAD_ROWS - 1) * KEYPAD_GAP == KEYPAD_H
                && KEYPAD_H == 284;
        });

        suite->addTest("keypad plus clear gap plus menu key fills the panel", []() {
            using namespace nero;
            return KEYPAD_H + KEYPAD_MENU_GAP + MENU_KEY_H == PANEL_H;
        });

        suite->addTest("menu key fits under the keypad", []() {
            using namespace nero;
            return MENU_KEY_W <= KEYPAD_COL_W && MENU_KEY_W == 130 && MENU_KEY_H == 70;
        });

        suite->addTest("keypad labels", []() {
            using namespace nero;
            return std::string(KEYPAD_LABELS[9]) == ":"
                && std::string(KEYPAD_LABELS[10]) == "0"
                && std::string(KEYPAD_LABELS[11]) == "."
                && std::string(KEYPAD_LABELS[12]) == "C"
                && std::string(KEYPAD_LABELS[13]) == ";"
                && isBackspaceLabel(KEYPAD_LABELS[14])
                && !isBackspaceLabel(KEYPAD_LABELS[12]);
        });

        suite->addTest("menu grid fills the panel", []() {
            using namespace nero;
            int w = 3 * menuKeyWidth() + 2 * MENU_GAP;
            return w <= PANEL_W && PANEL_W - w <= 2
                && 2 * menuKeyHeight() + MENU_GAP == PANEL_H;
        });

        suite->addTest("menu order and pages", []() {
            using namespace nero;
            const char* labels[6] = {"navigation", "segments", "date / time",
                                     "calibration", "settings", "exit"};
            const char* pages[5] = {"twinmaster", "stagesetup", "datetime",
                                    "calibration", "settings"};
            for (int i = 0; i < 6; ++i)
                if (std::string(MENU_ITEMS[i].label) != labels[i]) return false;
            for (int i = 0; i < 5; ++i)
                if (!MENU_ITEMS[i].page || std::string(MENU_ITEMS[i].page) != pages[i]) return false;
            return MENU_ITEMS[5].page == nullptr;
        });

        suite->addTest("menu labels are lower case", []() {
            using namespace nero;
            for (const MenuItem& m : MENU_ITEMS)
                for (const char* p = m.label; *p; ++p)
                    if (std::isupper(static_cast<unsigned char>(*p))) return false;
            return true;
        });

        return suite;
    }
};

#endif // TEST_NERO_FRAME_H
