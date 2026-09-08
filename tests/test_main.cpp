// Test Runner for Historic Rally Meter
// All tests are in separate header files from the functional implementation

#include "test_framework.h"
#include "test_config_file.h"
#include "test_distance.h"
#include "test_calibration.h"
#include "test_speed.h"
#include "test_counter_poller.h"
#include "test_segments.h"
#include "test_ahead_behind.h"
#include "test_rally_clock.h"
#include "test_edge_cases.h"
#include "test_calibration_independent.h"
#include "test_sim_counter.h"
#include "test_counter_poller_real.h"
#include "test_elapsed.h"
#include "test_distance_adjust.h"
#include "test_segment_row.h"
#include "test_beep_assist.h"
#include "test_calibration_display.h"
#include "test_stage_summary.h"
#include "test_gauge_layout.h"
#include "test_segment_entry_parsing.h"
#include "test_simple_tone.h"
#include "test_arrow_tone.h"

int main() {
    TestRunner runner;

    // Create test suites
    TestConfigFile configTests;
    TestDistance distanceTests;
    TestCalibration calibrationTests;
    TestSpeed speedTests;
    TestCounterPoller pollerTests;
    TestSegments segmentTests;
    TestAheadBehind aheadBehindTests;
    TestRallyClock clockTests;
    TestEdgeCases edgeCaseTests;
    TestCalibrationIndependent calIndepTests;
    TestSimCounter simCounterTests;
    TestCounterPollerReal realPollerTests;
    TestElapsed elapsedTests;
    TestDistanceAdjust distanceAdjustTests;
    TestSegmentRow segmentRowTests;
    TestBeepAssist beepAssistTests;
    TestCalibrationDisplay calDisplayTests;
    TestStageSummary stageSummaryTests;
    TestGaugeLayout gaugeLayoutTests;
    TestSegmentEntryParsing segmentEntryParsingTests;
    TestSimpleTone simpleToneTests;
    TestArrowTone arrowToneTests;

    // Add all test suites
    runner.addSuite(configTests.createSuite());
    runner.addSuite(distanceTests.createSuite());
    runner.addSuite(calibrationTests.createSuite());
    runner.addSuite(speedTests.createSuite());
    runner.addSuite(pollerTests.createSuite());
    runner.addSuite(segmentTests.createSuite());
    runner.addSuite(aheadBehindTests.createSuite());
    runner.addSuite(clockTests.createSuite());
    runner.addSuite(edgeCaseTests.createSuite());
    runner.addSuite(calIndepTests.createSuite());
    runner.addSuite(simCounterTests.createSuite());
    runner.addSuite(realPollerTests.createSuite());
    runner.addSuite(elapsedTests.createSuite());
    runner.addSuite(distanceAdjustTests.createSuite());
    runner.addSuite(segmentRowTests.createSuite());
    runner.addSuite(beepAssistTests.createSuite());
    runner.addSuite(calDisplayTests.createSuite());
    runner.addSuite(stageSummaryTests.createSuite());
    runner.addSuite(gaugeLayoutTests.createSuite());
    runner.addSuite(segmentEntryParsingTests.createSuite());
    runner.addSuite(simpleToneTests.createSuite());
    runner.addSuite(arrowToneTests.createSuite());

    // Run all tests
    int failures = runner.runAll();

    return (failures > 0) ? 1 : 0;
}
