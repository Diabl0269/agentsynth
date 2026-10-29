#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// TestModeInput.h: synthetic mouse and keyboard input for the test-mode server. Everything goes through the target's
// ComponentPeer (the same entry the OS uses), never into a component's mouseDown() directly and never through the OS
// event queue, so the real cursor and keyboard are untouched. Message thread only.
namespace synth::testmode {

/** Modifier names ("cmd", "shift", "alt", "ctrl") to a ModifierKeys; unknown names set `error`. */
juce::ModifierKeys modifiersFromNames(const juce::StringArray& names, juce::String& error);

/** "cmd+z", "space", "shift+tab", "escape" ... to a KeyPress. Empty `error` on success. */
juce::KeyPress keyPressFromText(const juce::String& text, juce::String& error);

enum class Button { left, right };

/** One mouse position and state in the peer of `target`: `local` is in target's coordinates. */
struct MouseStep {
    juce::Component* target = nullptr;
    juce::Point<int> local;
    juce::ModifierKeys keys; // key modifiers held (no mouse buttons)
    bool buttonDown = false;
    Button button = Button::left;
};

/** Delivers one mouse event through the peer. False (with `error`) when the target is not on a native window. */
bool sendMouse(const MouseStep& step, juce::String& error);

/** A press and release at `local` (clicks 1 or 2, the second quickly enough to count as a double click). */
bool click(juce::Component& target, juce::Point<int> local, Button button, int clicks, juce::ModifierKeys keys,
           juce::String& error);

/** Sends a key through the peer of `target`, which delivers it to the focused component (or the window). `handled`
 *  reports whether anything consumed it. False (with `error`) when the target is not on a native window. */
bool pressKey(juce::Component& target, const juce::KeyPress& key, bool& handled, juce::String& error);

/** The centre of a component, in its own coordinates. */
juce::Point<int> centreOf(const juce::Component& c);

} // namespace synth::testmode
