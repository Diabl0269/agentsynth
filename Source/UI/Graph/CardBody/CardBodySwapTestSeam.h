#pragma once

// CardBodySwapTestSeam.h -- test seams onto a card body's swap motion (CardBodySwapMotion.cpp) that take the card, so a
// test reaches them without including CardBody.h (and the card plan with it).

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

class ModuleComponent;

namespace synth::cardbody_test {

/** Every card's swap runs as if it were showing, also a card an undo or redo rebuilds. */
void setAnimateSwapOffScreen(bool animate);
/** Steps the card's running swap motion to `elapsedMs`. */
void stepSwap(ModuleComponent& card, double elapsedMs);
bool isSwapRunning(ModuleComponent& card);
/** The picture rectangles of the controls leaving the card now, in card pixels. */
std::vector<juce::Rectangle<float>> swapGhostRects(ModuleComponent& card);

struct ControlParts {
    juce::Component* widget = nullptr;
    juce::Component* label = nullptr; // null for a control with no caption
};
/** The widget and caption of the control for parameter `paramId`, or nullopt when the card has none. */
std::optional<ControlParts> controlParts(ModuleComponent& card, const juce::String& paramId);

} // namespace synth::cardbody_test
