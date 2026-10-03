#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// FocusStepWithin.h: arrow-key stepping inside a focus region whose root is a plain container (the timeline's routing
// pane, the piano roll's scale pane). Tab is the app's region cycle, so the controls inside such a region are reached
// with Up/Down from the root and left with Escape. Message thread only.
namespace synth::ui {

/** Every visible, enabled component under `root` that takes keyboard focus, in on-screen order (children in their own
 *  order, depth first). The root itself is not included. */
inline std::vector<juce::Component*> focusableDescendants(juce::Component& root) {
    std::vector<juce::Component*> stops;
    for (auto* child : root.getChildren()) {
        if (!child->isVisible())
            continue;
        // A viewport and its scroll bars take focus by default but are not controls.
        const bool chrome =
            dynamic_cast<juce::Viewport*>(child) != nullptr || dynamic_cast<juce::ScrollBar*>(child) != nullptr;
        if (!chrome && child->isEnabled() && child->getWantsKeyboardFocus())
            stops.push_back(child);
        for (auto* inner : focusableDescendants(*child))
            stops.push_back(inner);
    }
    return stops;
}

/** The control that follows (`direction` > 0) or precedes `current` in `root`; `current` null or not in the list means
 *  the root itself is where focus sits, so Down gives the first control and Up gives none. Never wraps. Null at an end.
 */
inline juce::Component* neighbourStop(juce::Component& root, juce::Component* current, int direction) {
    const auto stops = focusableDescendants(root);
    int index = -1;
    for (int i = 0; i < (int)stops.size(); ++i)
        if (stops[(size_t)i] == current)
            index = i;
    const int next = index + (direction > 0 ? 1 : -1);
    return next >= 0 && next < (int)stops.size() ? stops[(size_t)next] : nullptr;
}

/** Up/Down on the root (or bubbling from a control that ignores them): focus moves to the neighbouring control, and Up
 * off the first control returns it to the root. Always consumes the two keys. */
inline bool stepFocusWithin(juce::Component& root, int direction, juce::Component* current) {
    if (auto* next = neighbourStop(root, current, direction))
        next->grabKeyboardFocus();
    else if (direction < 0 && current != nullptr && current != &root)
        root.grabKeyboardFocus();
    return true;
}

/** The keys of a pane whose root is a plain container: bare Up/Down step to the neighbouring control and Escape returns
 *  focus to the root. A combo box or text field keeps its own Up/Down (it uses them), so only Escape leaves one. Attach
 * it to every control with attachToDescendants() once they exist; a control added later needs attach() too. */
class PaneKeys : public juce::KeyListener {
public:
    explicit PaneKeys(juce::Component& root)
        : root_(root) {}

    bool keyPressed(const juce::KeyPress& key, juce::Component* origin) override {
        if (origin == nullptr)
            return false;
        if (key == juce::KeyPress::escapeKey && origin != &root_) {
            root_.grabKeyboardFocus();
            return true;
        }
        if (usesArrows(*origin))
            return false;
        const bool plain = !key.getModifiers().isAnyModifierKeyDown();
        if (plain && key.isKeyCode(juce::KeyPress::upKey))
            return stepFocusWithin(root_, -1, origin);
        if (plain && key.isKeyCode(juce::KeyPress::downKey))
            return stepFocusWithin(root_, +1, origin);
        return false;
    }

    void attach(juce::Component& control) { control.addKeyListener(this); }
    void attachToDescendants() {
        for (auto* stop : focusableDescendants(root_))
            attach(*stop);
    }

private:
    static bool usesArrows(const juce::Component& c) {
        return dynamic_cast<const juce::ComboBox*>(&c) != nullptr ||
               dynamic_cast<const juce::TextEditor*>(&c) != nullptr;
    }

    juce::Component& root_;
};

} // namespace synth::ui
