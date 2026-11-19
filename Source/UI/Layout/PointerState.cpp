#include "UI/Layout/PointerState.h"

// PointerState.cpp -- the only file in Source/ that reads the raw OS pointer and live modifiers.

namespace synth::ui {

namespace {
struct Synthetic {
    std::optional<juce::Point<int>> position;
    std::optional<juce::ModifierKeys> modifiers;
};

Synthetic& synthetic() {
    static Synthetic state;
    return state;
}
} // namespace

juce::Point<int> pointerPosition() {
    if (const auto& at = synthetic().position)
        return *at;
    return juce::Desktop::getMousePosition();
}

juce::ModifierKeys currentModifiers() {
    if (const auto& mods = synthetic().modifiers)
        return *mods;
    return juce::ModifierKeys::getCurrentModifiersRealtime();
}

bool isAnyMouseButtonDown() { return currentModifiers().isAnyMouseButtonDown(); }

bool isPointerDragging() {
    if (synthetic().modifiers)
        return synthetic().modifiers->isAnyMouseButtonDown();
    return juce::Desktop::getInstance().getMainMouseSource().isDragging();
}

bool isKeyDown(int keyCode) { return juce::KeyPress::isKeyCurrentlyDown(keyCode); }

std::optional<juce::Point<int>> syntheticPointerPosition() { return synthetic().position; }

void setSyntheticPointer(juce::Point<int> screen, juce::ModifierKeys mods) {
    synthetic().position = screen;
    synthetic().modifiers = mods;
}

void clearSyntheticPointer() { synthetic() = {}; }

ScopedSyntheticModifiers::ScopedSyntheticModifiers(juce::ModifierKeys mods)
    : previous_(synthetic().modifiers) {
    synthetic().modifiers = mods;
}

ScopedSyntheticModifiers::~ScopedSyntheticModifiers() { synthetic().modifiers = previous_; }

} // namespace synth::ui
