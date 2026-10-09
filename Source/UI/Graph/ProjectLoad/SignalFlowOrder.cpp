// SignalFlowOrder.cpp -- Kahn's sort by level, with a deterministic cycle break and the output moved last.

#include "SignalFlowOrder.h"

#include <algorithm>
#include <queue>

namespace synth::ui {

namespace {

// Kahn's algorithm, taking free vertices lowest index first. A vertex's level is final when it is taken: it is one
// past the deepest feeder taken before it (cycle feeders taken later never count).
std::vector<int> kahnLevels(int n, const std::vector<std::vector<int>>& out, std::vector<int> inDegree) {
    std::vector<int> level((size_t)n, 0);
    std::vector<bool> taken((size_t)n, false);
    std::priority_queue<int, std::vector<int>, std::greater<>> ready;
    for (int v = 0; v < n; ++v)
        if (inDegree[(size_t)v] == 0)
            ready.push(v);
    int nextUntaken = 0;
    for (int placed = 0; placed < n; ++placed) {
        int v = -1;
        while (!ready.empty() && taken[(size_t)ready.top()])
            ready.pop();
        if (!ready.empty()) {
            v = ready.top();
            ready.pop();
        } else { // only cycles are left: break the lowest-index one
            while (taken[(size_t)nextUntaken])
                ++nextUntaken;
            v = nextUntaken;
        }
        taken[(size_t)v] = true;
        for (int w : out[(size_t)v]) {
            if (taken[(size_t)w])
                continue;
            level[(size_t)w] = std::max(level[(size_t)w], level[(size_t)v] + 1);
            if (--inDegree[(size_t)w] == 0)
                ready.push(w);
        }
    }
    return level;
}

} // namespace

std::vector<int> signalFlowLevels(int vertexCount, const std::vector<std::pair<int, int>>& edges,
                                  const std::vector<bool>& last) {
    const int n = std::max(0, vertexCount);
    std::vector<std::vector<int>> out((size_t)n);
    std::vector<int> inDegree((size_t)n, 0);
    for (const auto& [from, to] : edges) {
        if (from == to || from < 0 || to < 0 || from >= n || to >= n)
            continue;
        out[(size_t)from].push_back(to);
        ++inDegree[(size_t)to];
    }
    auto level = kahnLevels(n, out, std::move(inDegree));

    const auto isLast = [&last](int v) { return (size_t)v < last.size() && last[(size_t)v]; };
    int deepestOther = -1;
    int shallowestLast = -1;
    for (int v = 0; v < n; ++v) {
        if (isLast(v))
            shallowestLast = shallowestLast < 0 ? level[(size_t)v] : std::min(shallowestLast, level[(size_t)v]);
        else
            deepestOther = std::max(deepestOther, level[(size_t)v]);
    }
    if (shallowestLast >= 0 && shallowestLast <= deepestOther)
        for (int v = 0; v < n; ++v)
            if (isLast(v))
                level[(size_t)v] += deepestOther + 1 - shallowestLast;
    return level;
}

} // namespace synth::ui
