// Concern: BottomDockTabMotion -- the pure tab widths and looks while the strip gains or loses a tab.
#include "BottomDockTabMotion.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {
bool contains(const std::vector<DockView>& tabs, DockView tab) {
    return std::find(tabs.begin(), tabs.end(), tab) != tabs.end();
}
} // namespace

std::vector<BottomDockTabMotion::Tab> BottomDockTabMotion::mergedOrder(const std::vector<Tab>& before,
                                                                       const std::vector<Tab>& after) {
    auto merged = before;
    for (size_t i = 0; i < after.size(); ++i) {
        if (contains(merged, after[i]))
            continue;
        // After the nearest earlier tab of the new order that is already placed, else first.
        auto at = merged.begin();
        for (size_t p = i; p-- > 0;) {
            const auto found = std::find(merged.begin(), merged.end(), after[p]);
            if (found != merged.end()) {
                at = found + 1;
                break;
            }
        }
        merged.insert(at, after[i]);
    }
    return merged;
}

bool BottomDockTabMotion::begin(const std::vector<Tab>& before, const std::vector<Tab>& after, bool reducedMotion) {
    finish();
    if (before == after || before.empty() || after.empty())
        return false;
    for (const Tab tab : mergedOrder(before, after)) {
        Entry entry;
        entry.tab = tab;
        entry.leaving = !contains(after, tab);
        entry.restored = !contains(before, tab);
        entries_.push_back(entry);
    }
    // Slot sizes are fractions of the tab's full width: 1 -> 0 leaving, 0 -> 1 arriving, 1 throughout otherwise.
    exit_enter_slots::begin(entries_, 1.0f);
    timeline_ = exit_enter_slots::timelineFor(entries_);
    timeline_.outline = false; // an outline marks what an undo brought back; a returning tab just grows
    after_ = after;
    beforeCount_ = before.size();
    reduced_ = reducedMotion;
    running_ = true;
    applyAtMs(0.0);
    return true;
}

bool BottomDockTabMotion::applyAtMs(double elapsedMs) {
    if (!running_)
        return true;
    exit_enter_slots::apply(entries_, timeline_, elapsedMs, reduced_);
    gap_ = timeline_.at(elapsedMs).gap;
    return elapsedMs >= timeline_.totalMs();
}

void BottomDockTabMotion::finish() {
    entries_.clear();
    after_.clear();
    beforeCount_ = 0;
    gap_ = 0.0f;
    running_ = false;
}

std::vector<BottomDockTabMotion::Placement> BottomDockTabMotion::placements(juce::Rectangle<int> strip) const {
    std::vector<Placement> result;
    if (!running_ || strip.isEmpty())
        return result;
    const float width = static_cast<float>(strip.getWidth());
    const float oldWidth = width / static_cast<float>(beforeCount_);
    const float newWidth = width / static_cast<float>(after_.size());

    float start = static_cast<float>(strip.getX());
    int left = strip.getX();
    for (size_t i = 0; i < entries_.size(); ++i) {
        const auto& e = entries_[i];
        const float full = e.leaving ? oldWidth : e.restored ? newWidth : oldWidth + (newWidth - oldWidth) * gap_;
        start += full * e.current;
        // The last slot ends at the strip's edge whatever the rounding did.
        const int right = i + 1 == entries_.size() ? strip.getRight() : static_cast<int>(std::lround(start));
        Placement p;
        p.tab = e.tab;
        p.bounds = {left, strip.getY(), std::max(0, right - left), strip.getHeight()};
        p.scale = e.scale;
        p.alpha = e.alpha;
        p.leaving = e.leaving;
        p.visible = p.bounds.getWidth() > 0 && p.scale > 0.0f && p.alpha > 0.0f;
        result.push_back(p);
        left = std::max(left, right);
    }
    return result;
}

} // namespace synth::ui
