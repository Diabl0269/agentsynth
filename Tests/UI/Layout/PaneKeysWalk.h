#pragma once

// PaneKeysWalk.h: the walk every pane built on PaneKeys (UI/Layout/FocusStepWithin.h) is held to. Keys are delivered in
// the order juce::ComponentPeer::handleKeyPress uses: from the focused control up through its parents, each offering
// the key to its key listeners first and to its own keyPressed second, the first to use it ending the walk.
#include "UI/Layout/FocusStepWithin.h"
#include <algorithm>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace pane_walk {

inline juce::KeyPress plainKey(int code) { return juce::KeyPress(code, juce::ModifierKeys::noModifiers, 0); }

inline bool dispatchKey(synth::ui::PaneKeys& keys, juce::Component& root, juce::Component& origin,
                        const juce::KeyPress& press) {
    for (auto* target = &origin; target != nullptr; target = target->getParentComponent()) {
        if (keys.isAttachedTo(*target) && keys.keyPressed(press, &origin))
            return true;
        if (target->keyPressed(press))
            return true;
        if (target == &root)
            break;
    }
    return false;
}

/** Every focusable control of `root` is a key stop of the pane (never a part of a control), Down walks them in order,
 *  Up walks back and off the first returns to the root, and Escape returns to the root from each. A multi-line text
 *  editor keeps its own arrows and is the one stop a walk through Down and Up may not pass. */
inline void expectEveryControlReachedByDownAndUp(synth::ui::PaneKeys& keys, juce::Component& root) {
    const auto stops = synth::ui::focusableDescendants(root);
    ASSERT_FALSE(stops.empty());
    for (auto* stop : stops) {
        EXPECT_TRUE(keys.isAttachedTo(*stop)) << "the pane's keys never reach " << stop->getTitle();
        EXPECT_EQ(dynamic_cast<juce::Slider*>(stop->getParentComponent()), nullptr)
            << "a slider's own part is not a stop: " << stop->getTitle();
    }
    EXPECT_EQ(synth::ui::neighbourStop(root, nullptr, +1), stops.front()) << "Down from the root enters the first";
    EXPECT_TRUE(dispatchKey(keys, root, root, plainKey(juce::KeyPress::downKey)));
    EXPECT_TRUE(dispatchKey(keys, root, root, plainKey(juce::KeyPress::upKey)));

    for (size_t i = 0; i < stops.size(); ++i) {
        SCOPED_TRACE(stops[i]->getTitle());
        EXPECT_TRUE(dispatchKey(keys, root, *stops[i], plainKey(juce::KeyPress::downKey)));
        EXPECT_EQ(keys.getLastStepForTest(), i + 1 < stops.size() ? stops[i + 1] : nullptr);
        EXPECT_TRUE(dispatchKey(keys, root, *stops[i], plainKey(juce::KeyPress::upKey)));
        EXPECT_EQ(keys.getLastStepForTest(), i > 0 ? stops[i - 1] : &root);
        EXPECT_TRUE(dispatchKey(keys, root, *stops[i], plainKey(juce::KeyPress::escapeKey)));
    }
}

} // namespace pane_walk
