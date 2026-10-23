// SoundBrowserKeyboardReach.cpp -- how the keyboard reaches the Filters button, the chip row and the open panel's
// controls from the search box and the list. Tab is the app's region cycle, so inside the Sounds view Up and Down step
// through the header stops in on-screen order (the model of Layout/FocusStepWithin.h): search, Filters, the chip row
// while it shows, then every control of the open panel. Past the last stop Down lands on the list, and Up on the first
// row climbs back to the last stop. Left, Right and Space stay with the control that has them.
//
// A key reaches us as a key listener on the control (juce::ComponentPeer::handleKeyPress offers a component's key
// listeners before its own keyPressed), so a control that uses Up and Down itself must be refused here: a menu and a
// range handle keep their arrows, and Escape (handled by the panel) is how a keyboard user leaves them.
#include "UI/Layout/FocusStepWithin.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include <algorithm>

namespace synth::ui {

namespace {
bool usesUpAndDown(const juce::Component& c) {
    return dynamic_cast<const juce::ComboBox*>(&c) != nullptr || dynamic_cast<const juce::TextEditor*>(&c) != nullptr ||
           dynamic_cast<const RangeSlider*>(c.getParentComponent()) != nullptr;
}
} // namespace

std::vector<juce::Component*> SoundBrowserComponent::headerStops() {
    std::vector<juce::Component*> stops{&search_, &filtersButton_};
    if (chipRow_.isVisible())
        stops.push_back(&chipRow_);
    if (filtersOpen_ && panelHost_.isVisible())
        for (auto* control : focusableDescendants(filterPanel_))
            stops.push_back(control);
    return stops;
}

// Layout changes which controls are visible, never which exist, so listening is re-asserted after every layout.
void SoundBrowserComponent::attachReachListeners() {
    std::vector<juce::Component*> controls{&filtersButton_, &chipRow_};
    for (auto* control : focusableDescendants(filterPanel_))
        controls.push_back(control);
    for (auto* control : controls)
        if (std::find(reachAttached_.begin(), reachAttached_.end(), control) == reachAttached_.end()) {
            control->addKeyListener(this);
            reachAttached_.push_back(control);
        }
}

void SoundBrowserComponent::focusStop(juce::Component& stop) {
    lastFocusRequest_ = &stop;
    stop.grabKeyboardFocus();
    if (filterPanel_.isParentOf(&stop))
        scrollFocusedControlIntoView(&stop); // headless windows raise no focus change to do this for us
}

bool SoundBrowserComponent::keyPressed(const juce::KeyPress& key, juce::Component* origin) {
    if (origin == nullptr || key.getModifiers().isAnyModifierKeyDown() || usesUpAndDown(*origin))
        return false;
    const bool up = key.isKeyCode(juce::KeyPress::upKey);
    if (!up && !key.isKeyCode(juce::KeyPress::downKey))
        return false;
    const auto stops = headerStops();
    const auto at = std::find(stops.begin(), stops.end(), origin);
    if (at == stops.end() || origin == &search_)
        return false;
    const int index = (int)(at - stops.begin());
    if (up) {
        focusStop(*stops[(size_t)(index - 1)]);
        return true;
    }
    if (index + 1 < (int)stops.size()) {
        focusStop(*stops[(size_t)index + 1]);
        return true;
    }
    lastFocusRequest_ = this;
    focusList();
    if (selected_ < 0)
        moveSelection(+1);
    return true;
}

bool SoundBrowserComponent::climbToHeader() {
    focusStop(*headerStops().back());
    return true;
}

// The selection is on the first row that can be selected, or nothing can be selected at all.
bool SoundBrowserComponent::atFirstListRow() const {
    const int n = (int)rows_.size();
    if (selected_ < 0) {
        for (int i = 0; i < n; ++i)
            if (rows_[(size_t)i].navigable())
                return false;
        return true;
    }
    for (int i = 0; i < selected_; ++i)
        if (rows_[(size_t)i].navigable())
            return false;
    return true;
}

} // namespace synth::ui
