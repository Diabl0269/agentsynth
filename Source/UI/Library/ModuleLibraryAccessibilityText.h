#pragma once

#include <juce_core/juce_core.h>

// ModuleLibraryAccessibilityText.h: what a screen reader says for the module library's
// keyboard-focused row, e.g. "Oscillator, 3 of 12, Sources", and for the library as a whole when no
// row is focused. Pure, so the wording is unit-testable without a component or a native
// accessibility peer.

namespace synth::ui {

/** A module, snippet, plugin or command row: its name, its 1-based position among the rows of its
 *  section, and the section's name. The category is left out when it is empty. */
inline juce::String describeLibraryRowForAccessibility(const juce::String& name, int position, int count,
                                                       const juce::String& category) {
    juce::String text = name + ", " + juce::String(position) + " of " + juce::String(count);
    if (category.isNotEmpty())
        text += ", " + category;
    return text;
}

/** A section (or sub-section) header: its name, that it folds, whether it is open and how many rows it holds. */
inline juce::String describeLibrarySectionForAccessibility(const juce::String& name, bool collapsed, int itemCount) {
    return name + ", section, " + (collapsed ? "collapsed" : "expanded") + ", " + juce::String(itemCount) +
           (itemCount == 1 ? " item" : " items");
}

/** The library with no row focused: how many rows can be reached. */
inline juce::String describeLibraryForAccessibility(int rowCount) {
    return rowCount == 1 ? juce::String("1 item") : juce::String(rowCount) + " items";
}

} // namespace synth::ui
