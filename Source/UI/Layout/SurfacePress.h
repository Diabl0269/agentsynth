#pragma once

// SurfacePress.h -- whether a press on a selectable surface (a mixer strip) lands on one of its controls or on the
// surface itself. The rule: a press selects the surface unless some part on the way from the pressed component up to
// the surface handles a press there itself. A button, slider, text field or combo box always does; a part that acts
// on a press only in some places (a list's rows, not the space under them) answers through the surface's hook
// (docs/mixer/strip-focus.md#a-click-focuses-the-column).
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

/** A surface's own answer for its plain components that act on a press in some places (a list's rows, a divider that
 *  resizes): whether `part` handles a press at `local` (its coordinates). */
using PartHandlesPress = std::function<bool(const juce::Component& part, juce::Point<int> local)>;

/** Whether `c` is a control that always handles a press itself: a button, slider, text field or combo box. */
inline bool isPressControl(const juce::Component& c) {
    return dynamic_cast<const juce::Button*>(&c) != nullptr || dynamic_cast<const juce::Slider*>(&c) != nullptr ||
           dynamic_cast<const juce::TextEditor*>(&c) != nullptr || dynamic_cast<const juce::ComboBox*>(&c) != nullptr;
}

/** True when a press on `pressed` at `local` (its coordinates) is a press on `surface` itself: `pressed` is `surface`
 *  or inside it, and no part from `pressed` up to (not including) `surface` is a control or, by `partHandles`, acts on
 *  the press there. */
inline bool pressSelectsSurface(const juce::Component& surface, const juce::Component* pressed, juce::Point<int> local,
                                const PartHandlesPress& partHandles = {}) {
    if (pressed == nullptr || (pressed != &surface && !surface.isParentOf(pressed)))
        return false;
    for (auto* c = pressed; c != &surface; c = c->getParentComponent()) {
        if (isPressControl(*c) || (partHandles && partHandles(*c, local)))
            return false;
        local = c->getParentComponent()->getLocalPoint(c, local);
    }
    return true;
}

} // namespace synth::ui
