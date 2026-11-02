#include "stage_store.h"

bool validStageNumber(int n) { return n >= 1 && n <= RallyState::MAX_MEMORY_SLOTS; }

std::vector<int> programmedStages(const RallyState& s) {
    std::vector<int> out;
    for (int i = 0; i < RallyState::MAX_MEMORY_SLOTS; i++)
        if (!s.memory_slots[i].empty()) out.push_back(i + 1);
    return out;
}

int nextProgrammedStage(const RallyState& s, int from, int dir) {
    const int step = dir < 0 ? -1 : 1;
    for (int n = from + step; validStageNumber(n); n += step)
        if (!s.memory_slots[n - 1].empty()) return n;
    return from;
}

int chosenStageNumber(const std::string& text) {
    if (text.empty() || text.size() > 2) return 0;
    int n = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return 0;
        n = n * 10 + (c - '0');
    }
    return validStageNumber(n) ? n : 0;
}
