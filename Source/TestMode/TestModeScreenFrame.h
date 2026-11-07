#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

// TestModeScreenFrame.h: the `screenshot` id "screen" (docs/development/test-mode.md#commands): what a user sees, the
// main window with every other visible top-level window of the app and the pointer. Message thread only.
namespace synth::testmode {

/** One window drawn into a screen frame; `bounds` are in the frame's own (unscaled) coordinates. */
struct ScreenFrameWindow {
    juce::String id;
    juce::Rectangle<int> bounds;
};

struct ScreenFrame {
    juce::Image image;
    std::vector<ScreenFrameWindow> windows; // the main window first
    juce::Point<int> pointer;               // the pointer's tip, in the frame's unscaled coordinates
    juce::String pointerKind;               // "arrow", or the name of the custom cursor drawn ("tool:range")
};

/** Paints the main window and every other visible top-level window offscreen and composites them at their screen
 *  positions; `scale` enlarges the frame. Empty (with `error`) when there is no window to capture. */
std::optional<ScreenFrame> captureScreenFrame(float scale, juce::String& error);

/** The command reply: {path,width,height,windows:[{id,x,y,width,height}],pointer:{x,y,kind}}. */
juce::var describeScreenFrame(const ScreenFrame& frame, const juce::String& path);

} // namespace synth::testmode
