#include "UI/Layout/EditorTabWalk.h"

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/FocusRegion.h"
#include <algorithm>

namespace synth::ui {

EditorTabWalk::EditorTabWalk(juce::Component& home)
    : home_(home) {
    home_.addKeyListener(this);
}

EditorTabWalk::~EditorTabWalk() { home_.removeKeyListener(this); }

// The region cycle's own keys, so a rebind of Focus Next Region moves this walk too.
std::optional<bool> EditorTabWalk::cycleDirection(const juce::KeyPress& key) const {
    if (const auto* manager = shortcuts_ ? shortcuts_() : nullptr)
        return resolveFocusCycleKeyPress(key, *manager);
    if (key == juce::KeyPress(juce::KeyPress::tabKey, juce::ModifierKeys::noModifiers, 0))
        return true;
    if (key == juce::KeyPress(juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0))
        return false;
    return std::nullopt;
}

juce::Component* EditorTabWalk::focused() const {
    return hooks_.focused ? hooks_.focused() : juce::Component::getCurrentlyFocusedComponent();
}

// A control that cannot take focus now (hidden, greyed, laid out to nothing) is no stop: JUCE would refuse the grab.
std::vector<juce::Component*> EditorTabWalk::getStops() const {
    std::vector<juce::Component*> shown;
    if (!stops_)
        return shown;
    for (auto* stop : stops_())
        if (stop != nullptr && stop->isVisible() && stop->isEnabled() && stop->getWantsKeyboardFocus() &&
            !stop->getLocalBounds().isEmpty())
            shown.push_back(stop);
    return shown;
}

juce::Component* EditorTabWalk::focusedStop() const {
    auto* now = focused();
    const auto stops = getStops();
    return now != nullptr && std::find(stops.begin(), stops.end(), now) != stops.end() ? now : nullptr;
}

void EditorTabWalk::land(juce::Component& target, bool forward) {
    if (hooks_.moveFocusTo)
        hooks_.moveFocusTo(target);
    else
        target.grabKeyboardFocus();
    // Set after the grab: the grid's focusLost (a stop taking focus) resets the walk.
    if (&target == &home_)
        walkedRound_ = forward;
    else
        walkedRound_.reset();
}

// The listener sits on the grid, so a key reaches it from the grid and from every child the key bubbles through; the
// focused component says which one sent it. A child that is not a stop (a row name, a lane) keeps the region cycle.
bool EditorTabWalk::keyPressed(const juce::KeyPress& key, juce::Component*) {
    const auto direction = cycleDirection(key);
    if (!direction.has_value())
        return false;
    const bool forward = *direction;
    const auto stops = getStops();
    auto* now = focused();
    const auto at = std::find(stops.begin(), stops.end(), now);
    if (at != stops.end()) {
        const int next = (int)(at - stops.begin()) + (forward ? 1 : -1);
        if (next < 0 || next >= (int)stops.size())
            land(home_, forward); // past either end: back to the grid
        else
            land(*stops[(size_t)next], forward);
        return true;
    }
    if (now != nullptr && now != &home_)
        return false;
    // On the grid: the first Tab enters the header; one more in the direction a full walk just came round in leaves,
    // while the other direction walks the header back.
    if (stops.empty() || walkedRound_ == forward) {
        walkedRound_.reset();
        return false;
    }
    land(forward ? *stops.front() : *stops.back(), forward);
    return true;
}

} // namespace synth::ui
