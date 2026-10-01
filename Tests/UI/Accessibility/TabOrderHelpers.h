#pragma once

// TabOrderHelpers.h -- walks the Tab order of a component tree the way JUCE does when Tab is
// pressed (juce::KeyboardFocusTraverser, the same object Component::moveKeyboardFocusToSibling
// asks), so a test can assert the exact sequence of controls a keyboard user visits.
//
// Test-only (kept out of Source/ because nothing in the app calls it).

#include "AccessibilityAudit.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace synth::test {

// What a screen reader would call the control: its title, else a button's text. A control with
// neither prints as "<unnamed ClassName>", so a missing name fails an order assertion loudly.
inline juce::String tabStopName(const juce::Component& c) {
    if (c.getTitle().isNotEmpty())
        return c.getTitle();
    if (auto* button = dynamic_cast<const juce::Button*>(&c))
        if (button->getButtonText().isNotEmpty())
            return button->getButtonText();
    return "<unnamed " + audit_detail::className(c) + ">";
}

struct TabWalk {
    // Tab from the first stop, again and again, until the traverser reports no next stop.
    std::vector<juce::Component*> forward;
    // Shift+Tab from the last stop of `forward`, again and again.
    std::vector<juce::Component*> backward;
    // Every stop the traverser lists under the root, in order (what a wrap-around cycles through).
    std::vector<juce::Component*> all;

    juce::StringArray names() const {
        juce::StringArray out;
        for (auto* c : forward)
            out.add(tabStopName(*c));
        return out;
    }

    // Tab visits every stop once, Shift+Tab visits them in exactly the reverse order, and Tab from
    // the last stop has nowhere further to go inside the root (JUCE then wraps to the first).
    bool isCompleteCycle() const {
        if (forward.empty() || forward != all)
            return false;
        return std::equal(backward.begin(), backward.end(), forward.rbegin(), forward.rend());
    }
};

// `root` should be the top-level component of the surface under test (a dialog's content, a
// popup), so the traverser's focus container is `root` itself.
inline TabWalk walkTabOrder(juce::Component& root) {
    juce::KeyboardFocusTraverser traverser;
    TabWalk walk;
    walk.all = traverser.getAllComponents(&root);
    if (walk.all.empty())
        return walk;

    const auto limit = walk.all.size() + 2; // a loop in the traverser must fail, not hang
    // A stop outside `root` (a tab strip beside a tab's content) ends the walk: it is not part of it.
    for (auto* c = walk.all.front(); c != nullptr && root.isParentOf(c) && walk.forward.size() < limit;
         c = traverser.getNextComponent(c))
        walk.forward.push_back(c);
    for (auto* c = walk.forward.back(); c != nullptr && root.isParentOf(c) && walk.backward.size() < limit;
         c = traverser.getPreviousComponent(c))
        walk.backward.push_back(c);
    return walk;
}

// Escape, delivered through the surface's own keyPressed(), as the key reaches it from a focused child.
inline bool sendEscape(juce::Component& surface) {
    return surface.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
}
inline bool sendReturn(juce::Component& surface) {
    return surface.keyPressed(juce::KeyPress(juce::KeyPress::returnKey));
}

} // namespace synth::test
