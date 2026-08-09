#pragma once

#include "LayoutUtil.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <set>
#include <vector>

namespace synth::ui {

/**
 * @brief The set of graph nodes the user currently has selected on the canvas.
 *
 * Pure state + set algebra, deliberately free of any Component or graph dependency so the
 * multi-select rules are testable headlessly. GraphEditor owns one of these and is the only
 * thing that maps it onto ModuleComponents.
 *
 * Ids are held in a std::set keyed on NodeID::uid, so getSelected() always returns them in
 * ascending uid order — a stable order matters because it is what snippet extraction walks,
 * and a snippet's node order must not depend on click order.
 */
class SelectionModel {
public:
    using NodeID = juce::AudioProcessorGraph::NodeID;

    void clear() noexcept { ids.clear(); }
    bool isEmpty() const noexcept { return ids.empty(); }
    int size() const noexcept { return (int)ids.size(); }
    bool contains(NodeID id) const noexcept { return ids.find(id) != ids.end(); }

    /** Adds an id. NodeID{0} is the graph's "invalid node" sentinel and is never selectable.
     *  @return true if the id was newly added (false when already present or invalid). */
    bool add(NodeID id) {
        if (id.uid == 0)
            return false;
        return ids.insert(id).second;
    }

    /** @return true if the id was present and removed. */
    bool remove(NodeID id) { return ids.erase(id) > 0; }

    /** Adds the id when absent, removes it when present.
     *  @return the id's selected state AFTER the toggle. */
    bool toggle(NodeID id) {
        if (contains(id)) {
            remove(id);
            return false;
        }
        return add(id);
    }

    /** Replaces the whole selection. Invalid ids are dropped, duplicates collapse. */
    void setSelection(const std::vector<NodeID>& newIds) {
        ids.clear();
        for (auto id : newIds)
            add(id);
    }

    /** Selected ids in ascending uid order. */
    std::vector<NodeID> getSelected() const { return {ids.begin(), ids.end()}; }

    /** Drops every selected id that is not in `alive`. Call after any graph mutation that can
     *  remove nodes (delete, undo, preset load) so the selection can never name a freed node.
     *  @return true if anything was dropped. */
    bool retainOnly(const std::vector<NodeID>& alive) {
        std::set<NodeID> aliveSet(alive.begin(), alive.end());
        const auto before = ids.size();
        for (auto it = ids.begin(); it != ids.end();)
            it = (aliveSet.find(*it) == aliveSet.end()) ? ids.erase(it) : std::next(it);
        return ids.size() != before;
    }

private:
    std::set<NodeID> ids;
};

/** Normalises a drag anchor + current point into a positive-width/height rectangle, so a
 *  marquee dragged up-left behaves exactly like one dragged down-right. */
inline juce::Rectangle<int> marqueeRectFrom(juce::Point<int> anchor, juce::Point<int> current) {
    return juce::Rectangle<int>(anchor, current);
}

/** Ids of every box the marquee touches.
 *
 *  Intersection, not containment: clipping a module's edge selects it. That is the forgiving
 *  behaviour every node editor uses — requiring full enclosure makes wide modules (a 560 px
 *  Sequencer) practically unselectable at low zoom.
 *
 *  A degenerate (zero-width or zero-height) marquee touches nothing, so a plain Shift-click on
 *  empty canvas clears the selection instead of selecting whatever sits under the cursor.
 */
inline std::vector<SelectionModel::NodeID> hitTestMarquee(juce::Rectangle<int> marquee,
                                                          const std::vector<LayoutUtil::Box>& boxes) {
    std::vector<SelectionModel::NodeID> hits;
    if (marquee.isEmpty())
        return hits;

    for (const auto& box : boxes) {
        if (box.id.uid == 0)
            continue;
        if (marquee.intersects(box.rect))
            hits.push_back(box.id);
    }
    return hits;
}

/** Union of a base selection and a set of marquee hits, preserving base membership.
 *  Used for the additive (Cmd + Shift + drag) marquee. */
inline std::vector<SelectionModel::NodeID> unionSelection(const std::vector<SelectionModel::NodeID>& base,
                                                          const std::vector<SelectionModel::NodeID>& hits) {
    std::set<SelectionModel::NodeID> merged(base.begin(), base.end());
    merged.insert(hits.begin(), hits.end());
    return {merged.begin(), merged.end()};
}

} // namespace synth::ui
