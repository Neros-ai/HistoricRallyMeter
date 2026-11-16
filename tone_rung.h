#ifndef TONE_RUNG_H
#define TONE_RUNG_H

#include <algorithm>
#include <cmath>

// The stepping rule shared by tone type 1 (its beat gap) and tone type 2 (its
// pitch). The error is cut into 0.2 s steps ("rungs", 1..15, so 0.2 s .. 3.0 s;
// beyond 3 s the top rung holds). A growing error takes a rung the moment it
// reaches the line; a shrinking error keeps its rung until it is 0.1 s below
// the line. One rule in one place, so the two tones cannot drift apart.
constexpr double TONE_RUNG_STEP_S = 0.2;
constexpr double TONE_RUNG_HOLD_MARGIN_S = 0.1;  // a rung is held until the error is this far below its line
constexpr int    TONE_RUNG_COUNT = 15;

// The rung (1..15) for an error of this size, either sign.
inline int toneRung(double errorSeconds) {
    int step = static_cast<int>(std::floor(std::abs(errorSeconds) / TONE_RUNG_STEP_S + 1e-9));
    if (step < 1) step = 1;
    if (step > TONE_RUNG_COUNT) step = TONE_RUNG_COUNT;
    return step;
}

// The next held rung from the previous one (0 = none) and the LIVE error size.
inline int toneHeldRung(int held, double absErrorSeconds) {
    const int liveRung = toneRung(absErrorSeconds);
    const int downRung = toneRung(absErrorSeconds + TONE_RUNG_HOLD_MARGIN_S);
    return std::max(liveRung, std::min(held, downRung));
}

#endif // TONE_RUNG_H
