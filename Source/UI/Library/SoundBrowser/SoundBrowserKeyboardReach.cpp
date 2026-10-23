// SoundBrowserKeyboardReach.cpp -- how the keyboard reaches the Filters button, the chip row and the open panel's
// controls and the preview footer from the search box and the list. Tab is the app's region cycle, so inside the Sounds
// view Up and Down always step focus (the model of Layout/FocusStepWithin.h): search, Filters, the chip row while it
// shows, every control of the open panel, then the list, then the footer's controls. Past the last header stop Down
// lands on the list and Up on the first row climbs back; Down on the last row enters the footer and Up on its first
// control returns to the list. Only a text field keeps its own Up and Down.
//
// A key reaches us as a key listener on the control (juce::ComponentPeer::handleKeyPress offers a component's key
// listeners before its own keyPressed), so the steps win over a menu's or slider's own arrows. A menu opens with Return
// (its own) or Space (here, the macOS pop-up convention) and a handle or the volume moves with Left and Right.
#include "UI/Layout/FocusStepWithin.h"
#include "UI/Library/SoundBrowser/SoundBrowserComponent.h"
#include <algorithm>

namespace synth::ui {

namespace {
bool keepsUpAndDown(const juce::Component& c) { return dynamic_cast<const juce::TextEditor*>(&c) != nullptr; }
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
    for (auto* control : focusableDescendants(footer_))
        controls.push_back(control);
    for (auto* control : controls)
        if (std::find(reachAttached_.begin(), reachAttached_.end(), control) == reachAttached_.end()) {
            control->addKeyListener(this);
            reachAttached_.push_back(control);
        }
}

std::vector<juce::Component*> SoundBrowserComponent::footerStops() {
    return footer_.isVisible() ? focusableDescendants(footer_) : std::vector<juce::Component*>{};
}

void SoundBrowserComponent::focusStop(juce::Component& stop) {
    lastFocusRequest_ = &stop;
    stop.grabKeyboardFocus();
    if (filterPanel_.isParentOf(&stop))
        scrollFocusedControlIntoView(&stop); // headless windows raise no focus change to do this for us
}

bool SoundBrowserComponent::keyPressed(const juce::KeyPress& key, juce::Component* origin) {
    if (origin == nullptr || key.getModifiers().isAnyModifierKeyDown() || keepsUpAndDown(*origin))
        return false;
    if (key.isKeyCode(juce::KeyPress::spaceKey)) {
        auto* menu = dynamic_cast<juce::ComboBox*>(origin);
        if (menu == nullptr)
            return false;
        menu->showPopup();
        return true;
    }
    const bool up = key.isKeyCode(juce::KeyPress::upKey);
    if (!up && !key.isKeyCode(juce::KeyPress::downKey))
        return false;
    const auto footer = footerStops();
    if (const auto inFooter = std::find(footer.begin(), footer.end(), origin); inFooter != footer.end()) {
        if (!up && inFooter + 1 != footer.end())
            focusStop(**(inFooter + 1));
        else if (up && inFooter != footer.begin())
            focusStop(**(inFooter - 1));
        else if (up)
            focusListStop(false);
        return true;
    }
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
    focusListStop(true);
    return true;
}

// The list takes focus; `selectFirst` also selects its first row when nothing is selected (arriving from above).
void SoundBrowserComponent::focusListStop(bool selectFirst) {
    lastFocusRequest_ = this;
    focusList();
    if (selectFirst && selected_ < 0)
        moveSelection(+1);
}

bool SoundBrowserComponent::enterFooter() {
    const auto stops = footerStops();
    if (stops.empty())
        return false;
    focusStop(*stops.front());
    return true;
}

bool SoundBrowserComponent::climbToHeader() {
    focusStop(*headerStops().back());
    return true;
}

// No row that can be selected lies before (`direction` < 0) or after (> 0) the selection; with nothing selected, true
// only when no row can be selected at all.
bool SoundBrowserComponent::atListEnd(int direction) const {
    const int n = (int)rows_.size();
    const int from = selected_ < 0 ? (direction < 0 ? n : -1) : selected_;
    for (int i = from + direction; i >= 0 && i < n; i += direction)
        if (rows_[(size_t)i].navigable())
            return false;
    return true;
}

} // namespace synth::ui
