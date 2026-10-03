#pragma once

// What the on-card editor's "+ Add control" panel lists and how adding one changes the layout: read from
// the card's plan and written back as a pure layout change, with no UI.
// docs/layout/module-card-layout.md#editing-a-layout.

#include "Modules/CardLayout.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

namespace synth {
class CardBody;
}

namespace synth::ui {

/** A control the card does not show now: one the layout hid (the More row keeps its settings) or never placed. */
struct AddableControl {
    juce::String paramId;
    juce::String name; ///< What the card would call it (the label override, else the parameter's name).
};

/** Every control in `body`'s More row, in declaration order. */
std::vector<AddableControl> addableControls(const synth::CardBody& body);

/** The controls matching `query` (the app's one matcher), best match first; all of them for a blank query. */
std::vector<AddableControl> matchingControls(const std::vector<AddableControl>& controls, const juce::String& query);

/** "3 hidden controls", "1 hidden control", or with a query "2 of 3 hidden controls" / "No control matches". */
juce::String addCountText(int shown, int total, const juce::String& query);

/** Index of the last grid section of `layout` that is not the footer, or -1. */
int lastGridSectionIndex(const CardLayout& layout);

/** The index in the card's plan of `layout`'s section `index`: the plan lists the footer last. */
int planSectionIndexOf(const CardLayout& layout, int index);

/** The index in `layout` of the section the card's plan lists at `planIndex`, or -1. */
int layoutSectionIndexOfPlan(const CardLayout& layout, int planIndex);

/** `layout` with `paramId` shown at the end of its section `section` (-1 = the last grid group; one is made
 *  first when the layout has none), taking `at` as its free position when given. An item it already had
 *  keeps its widget, label and range; it leaves the hidden list and its old place. */
CardLayout withControlAdded(CardLayout layout, const juce::String& paramId, std::optional<juce::Point<int>> at,
                            int section = -1);

} // namespace synth::ui
