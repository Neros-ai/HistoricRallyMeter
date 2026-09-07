#include "arrow_tone.h"
#include "calculations.h"
#include <iostream>
#include <cmath>
#include <iomanip>

int main() {
    constexpr long CAL = 1000000000;
    constexpr double TARGET_100KPH = 100.0;
    
    // Test: ahead of schedule gives the slow-down direction
    std::cout << std::fixed << std::setprecision(10);
    
    double secondsAheadBehind = 2.0;
    double targetSpeedCountsPerHour = TARGET_100KPH;
    
    ArrowToneResult r = computeArrowBasedTone(secondsAheadBehind, targetSpeedCountsPerHour, CAL, false, 1000.0, false);
    
    std::cout << "Test: ahead of schedule gives the slow-down direction" << std::endl;
    std::cout << "Input: secondsAheadBehind=" << secondsAheadBehind << std::endl;
    std::cout << "Input: targetSpeedCountsPerHour=" << targetSpeedCountsPerHour << std::endl;
    std::cout << "Input: calibration=" << CAL << std::endl;
    std::cout << "\nResult:" << std::endl;
    std::cout << "  num_arrows=" << r.num_arrows << " (expected: 3)" << std::endl;
    std::cout << "  increase_speed=" << (r.increase_speed ? "true" : "false") << " (expected: false)" << std::endl;
    std::cout << "  tone_active=" << (r.tone_active ? "true" : "false") << std::endl;
    std::cout << "  freq_hz=" << r.freq_hz << std::endl;
    
    bool pass = r.num_arrows == 3 && r.increase_speed == false;
    std::cout << "\nTest " << (pass ? "PASSED" : "FAILED") << std::endl;
    
    return pass ? 0 : 1;
}
