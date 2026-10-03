// ModuleComponentKeyboard.cpp
//
// Stepping into a card from the keyboard (docs/layout/selection.md#keyboard). Return on the canvas
// (CanvasCardKeyboard) calls enterFromKeyboard(); from then on the card's controls hold focus,
// Tab/Shift+Tab walk them, and Escape -- or Tab past the last control / Shift+Tab before the
// first -- hands focus back to the canvas with the card still selected, so focus is never trapped
// inside a card. The knobs turn themselves (CardKnobSlider::keyPressed); a key a control leaves
// alone bubbles up to keyPressed() here before it can reach the canvas.

#include "ModuleComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <algorithm>
#include <tuple>

namespace {

bool isKeyboardStop(const juce::Component& c) {
    return dynamic_cast<const juce::Slider*>(&c) != nullptr || dynamic_cast<const juce::ComboBox*>(&c) != nullptr ||
           dynamic_cast<const juce::Button*>(&c) != nullptr || c.getWantsKeyboardFocus();
}

// A stock control is one stop: its own parts (a slider's value box, a combo's label) are not.
void collectStops(juce::Component& parent, std::vector<juce::Component*>& out) {
    for (auto* child : parent.getChildren()) {
        if (!child->isVisible() || !child->isEnabled() || child->getBounds().isEmpty())
            continue;
        if (isKeyboardStop(*child))
            out.push_back(child);
        else
            collectStops(*child, out);
    }
}

} // namespace

// Body controls first, top to bottom then left to right, so Return lands on the first knob, toggle
// or combo rather than on Bypass; the header buttons (Bypass, Mute, Delete, ...) close the cycle.
std::vector<juce::Component*> ModuleComponent::getKeyboardControls() {
    std::vector<juce::Component*> stops;
    collectStops(*this, stops);
    if (titleEditor != nullptr)
        stops.erase(std::remove(stops.begin(), stops.end(), titleEditor.get()), stops.end());

    // A mod dot sorts as its knob, just after it, so Tab visits the dot right after the control it modulates.
    auto key = [this](juce::Component* c) {
        int afterOwner = 0;
        if (auto* dot = dynamic_cast<synth::ui::ModDotButton*>(c); dot != nullptr && dot->getKnob() != nullptr) {
            c = dot->getKnob();
            afterOwner = 1;
        }
        const auto area = getLocalArea(c->getParentComponent(), c->getBounds());
        return std::make_tuple(area.getY() < kHeaderHeight ? 1 : 0, area.getY(), area.getX(), afterOwner);
    };
    std::stable_sort(stops.begin(), stops.end(),
                     [&key](juce::Component* a, juce::Component* b) { return key(a) < key(b); });
    return stops;
}

void ModuleComponent::focusForKeyboard(juce::Component* target) {
    if (target == nullptr)
        return;
    if (recordFocusForTest_)
        recordedFocus_ = target;
    else
        target->grabKeyboardFocus();
}

bool ModuleComponent::isOnKeyboardControl(const juce::Component* hit) {
    if (hit == nullptr || hit == this)
        return false;
    for (auto* stop : getKeyboardControls())
        if (stop == hit || stop->isParentOf(hit))
            return true;
    return false;
}

juce::Component* ModuleComponent::currentKeyboardFocus() const {
    return recordFocusForTest_ ? recordedFocus_.getComponent() : juce::Component::getCurrentlyFocusedComponent();
}

bool ModuleComponent::enterFromKeyboard() {
    const auto stops = getKeyboardControls();
    if (stops.empty())
        return false;
    focusForKeyboard(stops.front());
    return true;
}

// Only keys that arrive while focus is inside this card are acted on; everything else (Delete
// included) keeps bubbling to the canvas as before.
bool ModuleComponent::keyPressed(const juce::KeyPress& key) {
    auto* focused = currentKeyboardFocus();
    if (focused == nullptr || focused == this || !isParentOf(focused))
        return false;

    if (key == juce::KeyPress::escapeKey) {
        focusForKeyboard(&owner);
        return true;
    }

    const bool tab = key.getKeyCode() == juce::KeyPress::tabKey;
    const auto mods = key.getModifiers();
    if (!tab || mods.isCommandDown() || mods.isCtrlDown() || mods.isAltDown())
        return false;

    const auto stops = getKeyboardControls();
    if (stops.empty())
        return false;
    int index = -1;
    for (int i = 0; i < (int)stops.size(); ++i)
        if (stops[(size_t)i] == focused || stops[(size_t)i]->isParentOf(focused))
            index = i;
    const int count = (int)stops.size();
    const bool backward = mods.isShiftDown();
    // Stepping off either end leaves the card exactly as Escape does; a stop the card cannot place
    // (index < 0) enters at the first one.
    if (index >= 0 && (backward ? index == 0 : index == count - 1)) {
        focusForKeyboard(&owner);
        return true;
    }
    focusForKeyboard(stops[(size_t)(index < 0 ? 0 : index + (backward ? -1 : 1))]);
    return true;
}
