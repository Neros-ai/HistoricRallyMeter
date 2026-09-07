#ifndef TEST_ELAPSED_H
#define TEST_ELAPSED_H

#include "test_framework.h"
#include "../calculations.h"
#include <string>

// Tests for the co-pilot's elapsed-interval formatting. The key property is
// that the output carries no padding: columns are aligned by GTK label
// geometry, not by counting spaces, so the layout holds whichever monospace
// font the platform resolves.
class TestElapsed {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Elapsed Interval Formatting");

        suite->addTest("formats minutes and seconds with no padding", []() {
            return formatElapsedInterval(0) == "0:00"
                && formatElapsedInterval(48) == "0:48"
                && formatElapsedInterval(65) == "1:05";
        });

        suite->addTest("never emits a leading space", []() {
            // A leading space was how the old helper faked right-alignment.
            for (int64_t s : {0, 5, 59, 60, 599, 600, 5999, 60000}) {
                std::string out = formatElapsedInterval(s);
                if (!out.empty() && out[0] == ' ') return false;
            }
            return true;
        });

        suite->addTest("switches to hours:minutes past 9999 minutes", []() {
            // 10000 minutes = 600000 s -> 166 hours 40 minutes.
            return formatElapsedInterval(600000) == "166:40";
        });

        suite->addTest("reports toolong past 9999 hours", []() {
            // 10000 hours = 36000000 s. Nothing sensible fits the column.
            return formatElapsedInterval(36000000) == "toolong";
        });

        suite->addTest("clamps a negative interval to zero", []() {
            // A rally-clock adjustment can momentarily make now < start.
            return formatElapsedInterval(-30) == "0:00";
        });

        return suite;
    }
};

#endif // TEST_ELAPSED_H
