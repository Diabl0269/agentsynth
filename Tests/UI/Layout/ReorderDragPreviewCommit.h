#pragma once

// ReorderDragPreviewCommit.h -- the one helper behind every "the slot a reorder drag shows is the order the drop
// commits" test (DragSurfaceRegistryTests.cpp lists them). A real mouse-down, a path of drag events and a mouse-up go
// through the component's own handlers; the order drawn after the LAST drag event is read from where the items stand
// on screen, and the order after the release from the model that was written. Header-only; not registered in
// Tests/CMakeLists.txt.

#include <algorithm>
#include <functional>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <string>
#include <utility>
#include <vector>

namespace reorder_preview_commit {

enum class Phase { Down, Drag, Up };

/** What one gesture showed and wrote: the keys in drawn order after the last drag event, and in committed order after
 *  the release. */
template <class Key>
struct Outcome {
    std::vector<Key> preview;
    std::vector<Key> committed;
};

/** `send` turns a pointer position (in the space the caller chose) into the component's own mouse event of that phase.
 *  Events are rebuilt for every call, because the pressed item moves under the pointer. `shown` and `committed` read
 *  the keys in order. */
template <class Key>
struct Probe {
    std::function<void(Phase, juce::Point<float>)> send;
    std::function<std::vector<Key>()> shown;
    std::function<std::vector<Key>()> committed;
};

/** Presses at `press`, drags through every point of `path` (each is a drag event), reads the drawn order, releases on
 *  the last point and reads the committed order. */
template <class Key>
Outcome<Key> dragAndRelease(const Probe<Key>& probe, juce::Point<float> press,
                            const std::vector<juce::Point<float>>& path) {
    Outcome<Key> outcome;
    probe.send(Phase::Down, press);
    for (const auto& point : path)
        probe.send(Phase::Drag, point);
    outcome.preview = probe.shown();
    probe.send(Phase::Up, path.empty() ? press : path.back());
    outcome.committed = probe.committed();
    return outcome;
}

/** `count` drag points from `from` to `to`, evenly spaced, the last one exactly `to`. */
inline std::vector<juce::Point<float>> straightPath(juce::Point<float> from, juce::Point<float> to, int count = 6) {
    std::vector<juce::Point<float>> path;
    for (int i = 1; i <= count; ++i)
        path.push_back(from + (to - from) * ((float)i / (float)count));
    return path;
}

/** The keys sorted by where each is drawn (`position` = the item's top or left edge, or its centre). */
template <class Key, class Position>
std::vector<Key> orderedByPosition(std::vector<std::pair<Key, Position>> items) {
    std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.second < b.second; });
    std::vector<Key> keys;
    for (const auto& item : items)
        keys.push_back(item.first);
    return keys;
}

/** A name for a failure message: the keys joined by space. */
template <class Key, class Name>
std::string describe(const std::vector<Key>& keys, Name name) {
    std::string text;
    for (const auto& key : keys)
        text += (text.empty() ? "" : " ") + std::string(name(key));
    return text;
}

/** The common check: the drop commits what the last drag event showed, and the drag really moved something (so a
 *  test that drags nothing cannot pass by agreeing on the start order). */
template <class Key>
void expectCommitsWhatItPreviewed(const Outcome<Key>& outcome, const std::vector<Key>& before) {
    EXPECT_NE(outcome.preview, before) << "the drag showed no reorder";
    EXPECT_EQ(outcome.committed, outcome.preview) << "the drop committed another order than the one shown";
}

} // namespace reorder_preview_commit
