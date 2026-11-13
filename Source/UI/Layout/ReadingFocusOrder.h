#pragma once

#include <initializer_list>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** Tab visits `controls` in the order given, ahead of any sibling with no explicit order. JUCE sorts siblings by top
 *  edge, so a row whose controls differ in height is walked out of reading order unless it names the order; list every
 *  focusable sibling of the row, left to right (docs/development/accessibility.md, "Tab order follows reading
 *  order"). */
inline void setReadingFocusOrder(std::initializer_list<juce::Component*> controls) {
    int order = 1;
    for (auto* control : controls)
        control->setExplicitFocusOrder(order++);
}

} // namespace synth::ui
