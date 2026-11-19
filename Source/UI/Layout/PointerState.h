// PointerState.h -- the ONE place app code reads where the pointer is and which modifiers and buttons are held.
//
// The OS cursor and the live modifier state are global reads (juce::Desktop::getMousePosition,
// juce::ModifierKeys::getCurrentModifiersRealtime ...). Test-mode input goes through the window's peer and never moves
// either, so a popup, menu or drag that read them behaved differently from a real hand's (bug kind
// harness-mode-divergence). Every such read in Source/ goes through the helpers below instead. With no synthetic state
// set they return the real values, so the shipped app is unchanged; test mode sets the state on every synthetic event.
// A source-scan test (PointerStateGuardTests) fails when any other file calls the raw reads.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

/** Where the pointer is, in screen coordinates: the synthetic position when set, else the OS cursor. */
juce::Point<int> pointerPosition();

/** Modifier keys and mouse buttons held right now: the synthetic ones when set, else the OS's. */
juce::ModifierKeys currentModifiers();

/** True while any mouse button is held (a press the app has not seen released). */
bool isAnyMouseButtonDown();

/** True while the main pointer is dragging (a button held and moved). */
bool isPointerDragging();

/** True while the physical key is held. Test mode presses keys as events with no held state, so this is the real key.
 */
bool isKeyDown(int keyCode);

/** The synthetic pointer position, empty when none is set (the real pointer applies). */
std::optional<juce::Point<int>> syntheticPointerPosition();

/** Test-mode code only: the pointer is at `screen` with `mods` (key modifiers and any mouse button held). Called on
 *  every synthetic mouse event; stays set until cleared. */
void setSyntheticPointer(juce::Point<int> screen, juce::ModifierKeys mods = {});

/** Test-mode code only: the real pointer and modifiers apply again. */
void clearSyntheticPointer();

/** Test-mode code only: while alive, the modifiers read as `mods` (a synthetic key press carries its own); the
 *  position is untouched. */
class ScopedSyntheticModifiers {
public:
    explicit ScopedSyntheticModifiers(juce::ModifierKeys mods);
    ~ScopedSyntheticModifiers();
    ScopedSyntheticModifiers(const ScopedSyntheticModifiers&) = delete;
    ScopedSyntheticModifiers& operator=(const ScopedSyntheticModifiers&) = delete;

private:
    std::optional<juce::ModifierKeys> previous_;
};

} // namespace synth::ui
