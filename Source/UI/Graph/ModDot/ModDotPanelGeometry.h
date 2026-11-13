#pragma once

// Where the mod dot's panel goes and what it looks like: beside the dot (right of it, or left when there is no room),
// slid up or down to stay on screen while the arrow stays on the dot, and ONE outline that takes in the arrow.
// Pure geometry, so headless tests can pin it. docs/modules/modulation.md#the-mod-dot-menu.

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui::modDotPanel {

constexpr int kArrowLength = 8;   // how far the arrow reaches out of the panel's edge
constexpr int kArrowHalfBase = 7; // half the arrow's width where it meets the edge
constexpr int kGap = 2;           // between the arrow's tip and the dot
constexpr int kScreenMargin = 6;  // the panel never gets closer than this to the screen's edge
constexpr int kArrowInset = 24;   // preferred distance from the panel's top to the arrow, with room to spare
constexpr float kCorner = 8.0f;

struct Placement {
    juce::Rectangle<int> panel; // the panel body, in the same space as `dot` and `area`
    bool arrowOnLeft = true;    // the panel is right of the dot and its arrow points left
    int tipX = 0;
    int tipY = 0; // the dot's centre Y, unless the dot is too close to a corner of the panel for the arrow to reach
};

/** The panel of `size` for `dot`, inside `area`. Height is capped to the area; the arrow tip is level with the dot's
 *  centre whenever the panel's straight edge reaches it. */
Placement place(juce::Rectangle<int> dot, juce::Point<int> size, juce::Rectangle<int> area);

/** The tallest the panel can be inside `area`. */
int maxPanelHeight(juce::Rectangle<int> area);

/** One closed path: the rounded panel with the arrow cut into the edge facing the dot. */
juce::Path outline(const Placement& placement);

} // namespace synth::ui::modDotPanel
