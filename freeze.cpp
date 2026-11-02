#include "freeze.h"

void applyFreeze(RallyState& s, CounterPoller& p, bool on) {
    p.setHold(on);
    s.freeze_on = on;
    if (on) {
        s.freeze_virt1 = p.virtual1();
        s.freeze_virt2 = p.virtual2();
    } else {
        // Released: the travel skipped while frozen is now part of the
        // raw-minus-distance shift, so an ordinary restart lines up.
        s.reverse_shift1 = p.virtualShift1();
        s.reverse_shift2 = p.virtualShift2();
        s.freeze_virt1 = 0;
        s.freeze_virt2 = 0;
    }
}

void restoreFreeze(const RallyState& s, CounterPoller& p) {
    if (!s.freeze_on) return;
    p.setHold(true);
    p.setHeldVirtual(s.freeze_virt1, s.freeze_virt2);
}
