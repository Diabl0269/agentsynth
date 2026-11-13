// MacroHullGlide.cpp -- the border glide's arming rule and its per-edge offset.

#include "MacroHullGlide.h"

// A border already mid-glide is in `before` at its drawn position, so a second arm restarts it from where it
// is on screen rather than from where its first glide began.
bool MacroHullGlide::arm(const Hulls& before, const Hulls& after) {
    from_.clear();
    to_.clear();
    progress_ = 0.0f;
    for (const auto& [id, was] : before) {
        const auto it = after.find(id);
        if (it == after.end() || it->second == was || was.isEmpty() || it->second.isEmpty())
            continue;
        from_[id] = was;
        to_[id] = it->second;
    }
    return isLive();
}

void MacroHullGlide::applyTweenAt(float t) noexcept { progress_ = juce::jlimit(0.0f, 1.0f, t); }

void MacroHullGlide::finish() noexcept {
    from_.clear();
    to_.clear();
    progress_ = 1.0f;
}

// Each edge is offset from the LIVE border by what is left of its starting offset, rather than lerped
// between two fixed rectangles: the live border can keep moving during the glide (a member still being
// dragged, a neighbour pushed aside) and the drawn one must still land on it.
juce::Rectangle<int> MacroHullGlide::apply(const juce::String& macroId, juce::Rectangle<int> target) const {
    const auto from = from_.find(macroId);
    if (from == from_.end())
        return target;
    const auto& to = to_.at(macroId);
    const float left = 1.0f - progress_;
    const auto edge = [left](int live, int was, int settled) {
        return live + juce::roundToInt((was - settled) * left);
    };
    return juce::Rectangle<int>::leftTopRightBottom(edge(target.getX(), from->second.getX(), to.getX()),
                                                    edge(target.getY(), from->second.getY(), to.getY()),
                                                    edge(target.getRight(), from->second.getRight(), to.getRight()),
                                                    edge(target.getBottom(), from->second.getBottom(), to.getBottom()));
}
