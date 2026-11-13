#pragma once

// Puts a panel's content in its own window beside an anchor (ModDotPanelFrame): not modal (a canvas pick has to reach
// the canvas), closed by a press outside it, Esc or its owner, and deleted a turn after it is gone. The mod dot's
// panel and the port connections panel both go through it; what differs between them is passed in.

#include "ModDotPanelFrame.h"
#include <functional>
#include <memory>

namespace synth::ui {

struct ModDotPanelLaunch {
    /** Where the "Show info tooltips" preference lives, for the window's own tooltip (null: always on). */
    juce::ApplicationProperties* appProperties = nullptr;
    /** Told the tallest the content may be (the room on screen); null when the content never needs it. */
    std::function<void(int)> setMaxHeight;
    /** Handed the call that closes the window, for the content to run from its own Esc / dismiss. */
    std::function<void(std::function<void()>)> bindDismiss;
    /** Asked on a press outside the window; true keeps it open (a canvas pick under way). */
    std::function<bool()> keepOpenOnOutsideClick;
    /** The anchor is a region of a bigger component (a jack on a card): only a press inside the anchor's screen bounds
     *  is the owner's own click, not one anywhere on the component. */
    bool anchorIsRegion = false;
};

/** `anchorScreenBounds` is what the panel's arrow points at, in screen space. The frame deletes itself after it closes.
 */
ModDotPanelFrame* launchInFrame(std::unique_ptr<juce::Component> content, juce::Component& anchor,
                                juce::Rectangle<int> anchorScreenBounds, const ModDotPanelLaunch& options);

} // namespace synth::ui
