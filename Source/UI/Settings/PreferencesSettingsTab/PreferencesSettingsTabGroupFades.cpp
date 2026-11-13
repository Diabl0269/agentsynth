#include "PreferencesSettingsTab.h"
#include <algorithm>
#include <climits>

// Concern: the fade of a preference group that a filter or a fold takes out (or brings back), and the height tween
// that goes with it (docs/layout/animation.md#fading-things-in-and-out, docs/layout/settings-preferences.md).
//
// The layout units are unchanged: they lay a group out at its full size whenever `groupMatches` answers true, and
// `groupMatches` ends in `fadeGroup`, which answers true for a group that is fading out as well. That gives the
// pass its final-state positions with the leaving group still in its slot. `squashFadingGroups` then runs over the
// result and squeezes each fading group's slot (the rows, and the gap or divider above it) to `progress()` of its
// full height, so the rows below slide while the group fades and no row ever overlaps its neighbour.

namespace {
// Where a y in the full-size layout lands once every fading slot is squeezed.
struct SquashMap {
    struct Slot {
        int lead;   // top of the slot: the end of whatever sits above the group (its gap or divider included)
        int bottom; // bottom of the group's last row
        float progress;
    };
    std::vector<Slot> slots;

    int operator()(int y) const {
        float removed = 0.0f;
        for (const auto& s : slots) {
            if (y >= s.bottom) {
                removed += (float)(s.bottom - s.lead) * (1.0f - s.progress);
            } else if (y > s.lead) {
                return juce::roundToInt((float)s.lead - removed + (float)(y - s.lead) * s.progress);
            } else {
                break;
            }
        }
        return juce::roundToInt((float)y - removed);
    }

    // The slot a y falls into, or null.
    const Slot* slotAt(int y) const {
        for (const auto& s : slots)
            if (y >= s.lead && y < s.bottom)
                return &s;
        return nullptr;
    }

    bool overlaps(int top, int bottom) const {
        return std::any_of(slots.begin(), slots.end(),
                           [&](const Slot& s) { return top < s.bottom && bottom > s.lead; });
    }
};
} // namespace

bool PreferencesSettingsTab::fadeGroup(std::initializer_list<juce::Component*> comps, bool target) {
    auto* key = *comps.begin();
    auto found = groupFades_.find(key);
    const bool firstSight = found == groupFades_.end();
    if (firstSight) {
        found = groupFades_.emplace(key, GroupFade{}).first;
        auto& created = found->second;
        created.comps.assign(comps.begin(), comps.end());
        created.fade = std::make_unique<synth::ui::FadeVisibility>(comps);
        relayoutUpdater_.run = [this] { resized(); };
        created.fade->onFrame = [this] { relayoutUpdater_.triggerAsyncUpdate(); };
    }
    auto& group = found->second;
    // What is there when the tab is first laid out does not fade in or out.
    if (firstSight)
        group.fade->snapTo(target);
    else
        group.fade->setShown(target);
    if (std::find(laidOutGroups_.begin(), laidOutGroups_.end(), &group) == laidOutGroups_.end())
        laidOutGroups_.push_back(&group);
    return target || group.fade->isFading();
}

bool PreferencesSettingsTab::anyGroupFadingForTest() const {
    return std::any_of(groupFades_.begin(), groupFades_.end(),
                       [](const auto& entry) { return entry.second.fade->isFading(); });
}

// Runs after the whole pass, headers included, so every position is the full-size one.
void PreferencesSettingsTab::squashFadingGroups() {
    dividerAlphas_.assign(dividerBounds.size(), 1.0f);

    std::vector<juce::Component*> kids;
    for (auto* child : contentHost.getChildren())
        if (child->isVisible())
            kids.push_back(child);

    SquashMap map;
    for (auto* group : laidOutGroups_) {
        if (!group->fade->isFading())
            continue;
        int top = INT_MAX;
        int bottom = INT_MIN;
        for (auto* c : group->comps)
            if (c->isVisible()) {
                top = juce::jmin(top, c->getY());
                bottom = juce::jmax(bottom, c->getBottom());
            }
        if (bottom <= top)
            continue;
        // The slot starts where whatever sits above the group ends, so the gap or divider above it squeezes too.
        int lead = 0;
        for (auto* k : kids)
            if (k->getBottom() <= top && std::find(group->comps.begin(), group->comps.end(), k) == group->comps.end())
                lead = juce::jmax(lead, k->getBottom());
        map.slots.push_back({lead, bottom, group->fade->progress()});
    }
    if (map.slots.empty())
        return;
    std::sort(map.slots.begin(), map.slots.end(), [](const auto& a, const auto& b) { return a.lead < b.lead; });

    for (auto* k : kids) {
        const int top = k->getY();
        const int bottom = k->getBottom();
        if (map.overlaps(top, bottom))
            k->setBounds(k->getX(), map(top), k->getWidth(), juce::jmax(0, map(bottom) - map(top)));
        else
            k->setTopLeftPosition(k->getX(), map(top)); // outside every slot: it only moves, its size is untouched
    }
    for (size_t i = 0; i < dividerBounds.size(); ++i) {
        auto& divider = dividerBounds[i];
        if (const auto* slot = map.slotAt(divider.getY()))
            dividerAlphas_[i] = slot->progress;
        divider.setY(map(divider.getY()));
    }
    contentHost.setSize(contentHost.getWidth(), juce::jmax(1, map(contentHost.getHeight())));
    contentHost.repaint();
}
