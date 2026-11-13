#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** Where the parts of a snippet row sit across a row `contentWidth` wide, relative to the row's top `rowY`. */
struct SnippetRowRects {
    juce::Rectangle<int> label; ///< stops before the count, so a long name truncates
    juce::Rectangle<int> count; ///< "(n)", left of the badge
    juce::Rectangle<int> badge; ///< the Yours badge, at the row's right margin like a Sounds shape row's
};

/** The row's right margin: the same 10 px the plugin rows keep. */
inline constexpr int kSnippetRowRightMargin = 10;

SnippetRowRects snippetRowRects(int contentWidth, int rowY, int rowHeight, int countTextWidth);

} // namespace synth::ui
