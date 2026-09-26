// ut_costprobe.h - the cost meter's arithmetic and its text (pure; test_viewgate).
//
// The mod's OWN time per frame, measured with QueryPerformanceCounter in hooks.cpp (no allocation,
// no per-frame log): the Present side (panelDraw, the page draw PRE / POST - the slot tint refresh
// runs inside the PRE - and the tooltip detours; summed per frame, closed at each Present) and the
// game side (viewTick per GameEngine::Update: the rebuilds, the owned scan). Every 5 seconds the
// existing `frames:` line gets the window's figures appended:
//   frames: total=... +N in 5.0s (143.8 fps); mod: present avg 0.08 / max 0.61 ms, update avg 0.02
//   / max 12.70 ms, rebuilds 3 (max 12.7 ms), scan 1 (max 4.1 ms)
// And ONE line per session gives the cost of the first three rebuilds and the first owned scan.
#pragma once

#include <stdio.h>

namespace ut {

struct UtCostWindow {
    long long sum;   // QPC ticks
    long long max;
    long n;          // frames / updates / rebuilds / scans in the window
};

inline double utCostMs(long long ticks, long long freq) {
    return freq > 0 ? (double)ticks * 1000.0 / (double)freq : 0.0;
}
inline double utCostAvgMs(const UtCostWindow& w, long long freq) {
    return w.n > 0 ? utCostMs(w.sum, freq) / (double)w.n : 0.0;
}

// "mod: present avg a / max b ms, update avg c / max d ms, rebuilds N (max X ms), scan N (max Y ms)"
inline int utCostFormat(char* out, unsigned cap, const UtCostWindow& present,
                        const UtCostWindow& update, const UtCostWindow& rebuild,
                        const UtCostWindow& scan, long long freq) {
    if (!out || cap == 0) return 0;
    const int r = _snprintf_s(out, cap, _TRUNCATE,
                              "mod: present avg %.2f / max %.2f ms, update avg %.2f / max %.2f ms, "
                              "rebuilds %ld (max %.1f ms), scan %ld (max %.1f ms)",
                              utCostAvgMs(present, freq), utCostMs(present.max, freq),
                              utCostAvgMs(update, freq), utCostMs(update.max, freq), rebuild.n,
                              utCostMs(rebuild.max, freq), scan.n, utCostMs(scan.max, freq));
    return r < 0 ? (int)cap - 1 : r;
}

// The once-per-session line: the first three rebuilds and the first owned scan. Each note answers
// true exactly once in a session - when both are complete (whichever comes last says it).
struct UtCostFirst {
    double rebuild[3];
    int rebuilds;
    double scan;
    bool haveScan;
    bool said;
};
inline bool utCostFirstDue(UtCostFirst& f) {
    if (f.said || f.rebuilds < 3 || !f.haveScan) return false;
    f.said = true;
    return true;
}
inline bool utCostFirstRebuild(UtCostFirst& f, double ms) {
    if (f.rebuilds < 3) f.rebuild[f.rebuilds++] = ms;
    return utCostFirstDue(f);
}
inline bool utCostFirstScan(UtCostFirst& f, double ms) {
    if (!f.haveScan) {
        f.haveScan = true;
        f.scan = ms;
    }
    return utCostFirstDue(f);
}

}  // namespace ut
