#ifndef TEST_SEGMENT_ENTRY_PARSING_H
#define TEST_SEGMENT_ENTRY_PARSING_H

#include "test_framework.h"
#include "../calculations.h"

// Tests for the RB-SEG-03 fix: entering multiple ';'-separated speeds
// alongside multiple ';'-separated distances in the Stage Setup screen
// must pair them positionally instead of silently reusing only the first
// parsed speed for every segment (the pre-fix bug: std::stod("30;40")
// silently parses only "30" and ignores everything after the first
// non-numeric character).
class TestSegmentEntryParsing {
public:
    TestSuite* createSuite() {
        auto* suite = new TestSuite("Segment Entry Parsing (RB-SEG-03)");

        suite->addTest("parseSemicolonList splits and parses each token", []() {
            std::vector<double> result = parseSemicolonList("30;40;50");
            return result.size() == 3
                && std::abs(result[0] - 30.0) < 0.001
                && std::abs(result[1] - 40.0) < 0.001
                && std::abs(result[2] - 50.0) < 0.001;
        });

        suite->addTest("parseSemicolonList handles a single value with no ';'", []() {
            std::vector<double> result = parseSemicolonList("30");
            return result.size() == 1 && std::abs(result[0] - 30.0) < 0.001;
        });

        suite->addTest("parseSemicolonList skips a malformed token instead of throwing", []() {
            // The stage-setup keypad offers "." and ";" as well as digits, and
            // both the speed and distance entries feed through here. An
            // unguarded std::stod throws out of a GTK "clicked" handler --
            // C code with no C++ handler in the stack -- which terminates the
            // app from a stray keypress mid-rally.
            std::vector<double> result = parseSemicolonList("50;.;30");
            ASSERT_EQ(result.size(), 2u);
            ASSERT_NEAR(result[0], 50.0, 0.001);
            ASSERT_NEAR(result[1], 30.0, 0.001);
            return true;
        });

        suite->addTest("parseSemicolonList rejects a token with trailing junk", []() {
            // "1.2.3" silently read as 1.2 is a wrong segment, not a typo the
            // operator can see. Same contract as parseBeepWaypointsKm.
            std::vector<double> result = parseSemicolonList("1.2.3;40");
            ASSERT_EQ(result.size(), 1u);
            ASSERT_NEAR(result[0], 40.0, 0.001);
            return true;
        });

        suite->addTest("parseSemicolonList survives an all-rubbish list", []() {
            return parseSemicolonList(".;-;;abc").empty();
        });

        suite->addTest("parseSemicolonList skips empty tokens", []() {
            std::vector<double> result = parseSemicolonList("30;;40");
            return result.size() == 2
                && std::abs(result[0] - 30.0) < 0.001
                && std::abs(result[1] - 40.0) < 0.001;
        });

        suite->addTest("single speed broadcasts to every distance (existing use case)", []() {
            std::vector<double> speeds = {30.0};
            std::vector<double> distances = {1000.0, 2000.0, 3000.0};
            std::vector<std::pair<double,double>> pairs;
            bool ok = buildSegmentSpeedDistancePairs(speeds, distances, pairs);
            return ok && pairs.size() == 3
                && pairs[0] == std::make_pair(30.0, 1000.0)
                && pairs[1] == std::make_pair(30.0, 2000.0)
                && pairs[2] == std::make_pair(30.0, 3000.0);
        });

        suite->addTest("matching multi-speed/multi-distance counts pair positionally", []() {
            // This is the exact bug report: 30;40 kph with 1000;2000 m must
            // produce [30@1000, 40@2000], not [30@1000, 30@2000].
            std::vector<double> speeds = {30.0, 40.0};
            std::vector<double> distances = {1000.0, 2000.0};
            std::vector<std::pair<double,double>> pairs;
            bool ok = buildSegmentSpeedDistancePairs(speeds, distances, pairs);
            return ok && pairs.size() == 2
                && pairs[0] == std::make_pair(30.0, 1000.0)
                && pairs[1] == std::make_pair(40.0, 2000.0);
        });

        suite->addTest("mismatched multi-speed/multi-distance counts are rejected", []() {
            std::vector<double> speeds = {30.0, 40.0, 50.0};
            std::vector<double> distances = {1000.0, 2000.0};
            std::vector<std::pair<double,double>> pairs;
            bool ok = buildSegmentSpeedDistancePairs(speeds, distances, pairs);
            return !ok && pairs.empty();
        });

        suite->addTest("zero speeds is rejected", []() {
            std::vector<double> speeds = {};
            std::vector<double> distances = {1000.0};
            std::vector<std::pair<double,double>> pairs;
            bool ok = buildSegmentSpeedDistancePairs(speeds, distances, pairs);
            return !ok && pairs.empty();
        });

        return suite;
    }
};

#endif // TEST_SEGMENT_ENTRY_PARSING_H
